#include "SqliteRepository.h"
#include "SqliteInternal.h"
#include "Validation.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <openssl/evp.h>
#include <set>
#include <random>
#include <vector>
#include <type_traits>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using storage::Connection;
using storage::Failure;
using storage::Statement;
using storage::Transaction;

namespace {
template <class F> auto guarded(F &&operation) -> lexicon::Result<std::invoke_result_t<F>> {
  using T = std::invoke_result_t<F>;
  try {
    if constexpr (std::is_void_v<T>) { operation(); return {}; }
    else return operation();
  } catch (const Failure &error) {
    return std::unexpected(lexicon::Error{error.code, error.what()});
  } catch (const std::exception &error) {
    return std::unexpected(lexicon::Error{lexicon::Error::Code::Storage, error.what()});
  }
}
void require(bool condition, const std::string &message,
             lexicon::Error::Code code = lexicon::Error::Code::Validation) {
  if (!condition) throw Failure(message, code);
}
void valid(const lexicon::Result<void> &result) {
  if (!result) throw Failure(result.error().message, result.error().code);
}
void requireChanged(const Connection &db, const char *record) {
  if (db.changes() == 0)
    throw Failure(std::string(record) + " not found.", lexicon::Error::Code::NotFound);
}
std::vector<std::string> strings(const Connection &db, const std::string &sql, int id) {
  Statement stmt(db, sql); stmt.bind(id);
  std::vector<std::string> result;
  while (stmt.step()) result.push_back(stmt.text(0));
  return result;
}
void logOperation(const Connection &db, const char *table, int id, int type) {
  Statement(db, "INSERT INTO log(table_name, record_id, log_type) VALUES(?, ?, ?);")
      .bind(table).bind(id).bind(type).run();
}
void replaceStrings(const Connection &db, const char *table, const char *column,
                    int itemId, const std::vector<std::string> &values) {
  Statement(db, std::string("DELETE FROM ") + table + " WHERE item_id = ?;").bind(itemId).run();
  Statement insert(db, std::string("INSERT INTO ") + table + "(item_id, " + column + ") VALUES(?, ?);");
  for (const auto &value : lexicon::cleanedUniqueValues(values)) {
    insert.bind(itemId).bind(value).run(); insert.reset();
  }
}
std::vector<std::string> jsonArray(const Connection &db, const std::string &json) {
  Statement validJson(db, "SELECT json_valid(?), json_type(?)");
  validJson.bind(json).bind(json);
  if (!validJson.step() || !validJson.integer(0) || validJson.text(1) != "array")
    throw Failure("Invalid enum options JSON.");
  Statement stmt(db, "SELECT value, type FROM json_each(?) ORDER BY CAST(key AS INTEGER)");
  stmt.bind(json);
  std::vector<std::string> values;
  while (stmt.step()) if (stmt.text(1) == "text") values.push_back(stmt.text(0));
  return values;
}
std::string toJsonArray(const Connection &db, const std::vector<std::string> &values) {
  Statement quote(db, "SELECT json_quote(?)");
  std::string json = "[";
  for (const auto &value : values) {
    if (json.size() > 1) json += ',';
    quote.bind(value);
    if (!quote.step()) throw Failure("JSON quoting failed.");
    json += quote.text(0);
    quote.reset();
  }
  return json + ']';
}
std::vector<lexicon::ItemFieldRecord> fieldsFor(const Connection &db, int typeId) {
  Statement stmt(db, "SELECT id, item_type_id, name, data_type, position, enum_options "
                     "FROM item_field WHERE item_type_id = ? ORDER BY position, name COLLATE NOCASE, id;");
  stmt.bind(typeId);
  std::vector<lexicon::ItemFieldRecord> fields;
  while (stmt.step()) {
    lexicon::ItemFieldRecord field;
    field.id = stmt.integer(0); field.itemTypeId = stmt.integer(1); field.name = stmt.text(2);
    field.dataType = static_cast<lexicon::FieldDataType>(stmt.integer(3));
    field.position = stmt.integer(4); field.enumOptions = jsonArray(db, stmt.text(5));
    fields.push_back(std::move(field));
  }
  return fields;
}
std::vector<std::string> splitJoined(const std::string &joined) {
  std::vector<std::string> result;
  std::size_t start = 0;
  while (start < joined.size()) {
    auto end = joined.find(", ", start);
    auto value = joined.substr(start, end == std::string::npos ? end : end - start);
    if (!value.empty()) result.push_back(value);
    if (end == std::string::npos) break;
    start = end + 2;
  }
  return result;
}
} // namespace

struct SqliteRepository::Impl {
  Connection db;
  std::string path;
  enum class UnitState { Idle, Active, Failed } unitState = UnitState::Idle;
};
SqliteRepository::SqliteRepository() : impl_(std::make_unique<Impl>()) {}
SqliteRepository::~SqliteRepository() = default;

SqliteRepository::Result<void> SqliteRepository::open(const std::string &path) {
  return guarded([&] {
    impl_->unitState = Impl::UnitState::Idle;
    impl_->path.clear();
    try {
      impl_->db.open(path);
      storage::applyMigrations(impl_->db);
      impl_->path = path;
    } catch (...) {
      impl_->db.close();
      throw;
    }
  });
}
SqliteRepository::Result<std::map<std::string, std::string>> SqliteRepository::loadConfiguration() {
  return guarded([&] {
    Statement stmt(impl_->db, "SELECT \"key\", value FROM configuration;");
    std::map<std::string, std::string> result;
    while (stmt.step()) result.emplace(stmt.text(0), stmt.text(1));
    return result;
  });
}
SqliteRepository::Result<void> SqliteRepository::saveConfiguration(const std::map<std::string, std::string> &values) {
  return guarded([&] {
    if (values.empty()) return;
    Transaction tx(impl_->db, "lexicon_write");
    Statement stmt(impl_->db, "INSERT INTO configuration(\"key\", value) VALUES(?, ?) "
                              "ON CONFLICT(\"key\") DO UPDATE SET value = excluded.value;");
    for (const auto &[key, value] : values) {
      require(!lexicon::trim(key).empty(), "Configuration key cannot be empty.");
      stmt.bind(key).bind(value).run(); stmt.reset();
    }
    tx.commit();
  });
}
SqliteRepository::Result<std::vector<SqliteRepository::GroupRecord>> SqliteRepository::loadGroups() {
  return guarded([&] {
    Statement stmt(impl_->db, "SELECT id, name, description, position FROM item_group "
                               "ORDER BY position, name COLLATE NOCASE, id;");
    std::vector<GroupRecord> groups;
    while (stmt.step()) groups.push_back({stmt.integer(0), stmt.text(1), stmt.text(2), stmt.integer(3)});
    return groups;
  });
}
SqliteRepository::Result<int> SqliteRepository::defaultGroupId() {
  return guarded([&] {
    Statement find(impl_->db, "SELECT id FROM item_group WHERE name = 'Default' LIMIT 1;");
    if (find.step()) return find.integer(0);
    Transaction tx(impl_->db, "lexicon_write");
    Statement insert(impl_->db, "INSERT INTO item_group(name, description, position) VALUES('Default', ?, (SELECT COALESCE(MAX(position) + 1, 0) FROM item_group));");
    insert.bind("Default group for new items when no group is selected.").run();
    int id = impl_->db.lastId();
    logOperation(impl_->db, "item_group", id, 1);
    tx.commit();
    return id;
  });
}
SqliteRepository::Result<void> SqliteRepository::upsertGroup(const GroupRecord &group) {
  return guarded([&] {
    valid(lexicon::validateGroup(group));
    Transaction tx(impl_->db, "lexicon_write");
    int id = group.id;
    if (id < 0) {
      Statement(impl_->db, "INSERT INTO item_group(name, description, position) VALUES(?, ?, ?);")
          .bind(lexicon::trim(group.name)).bind(lexicon::trim(group.description)).bind(group.position).run();
      id = impl_->db.lastId();
    } else {
      Statement(impl_->db, "UPDATE item_group SET name = ?, description = ?, position = ? WHERE id = ?;")
          .bind(lexicon::trim(group.name)).bind(lexicon::trim(group.description)).bind(group.position).bind(id).run();
      requireChanged(impl_->db, "Group");
    }
    logOperation(impl_->db, "item_group", id, group.id < 0 ? 1 : 2);
    tx.commit();
  });
}
SqliteRepository::Result<void> SqliteRepository::deleteGroup(int groupId) {
  return guarded([&] {
    Transaction tx(impl_->db, "lexicon_write");
    Statement(impl_->db, "DELETE FROM item_group WHERE id = ?;").bind(groupId).run();
    requireChanged(impl_->db, "Group");
    logOperation(impl_->db, "item_group", groupId, 3); tx.commit();
  });
}
SqliteRepository::Result<std::vector<SqliteRepository::ItemTypeRecord>> SqliteRepository::loadItemTypes(int groupId) {
  return guarded([&] {
    std::string sql = "SELECT ty.id, ty.group_id, COALESCE(g.name, ''), ty.name, ty.description "
                      "FROM item_type ty LEFT JOIN item_group g ON g.id = ty.group_id ";
    if (groupId > 0) sql += "WHERE ty.group_id IS NULL OR ty.group_id = ? ";
    sql += "ORDER BY ty.name COLLATE NOCASE, ty.group_id IS NULL, g.name COLLATE NOCASE, ty.id;";
    Statement stmt(impl_->db, sql);
    if (groupId > 0) stmt.bind(groupId);
    std::vector<ItemTypeRecord> types;
    while (stmt.step()) types.push_back({stmt.integer(0), stmt.isNull(1) ? -1 : stmt.integer(1),
                                        stmt.text(2), stmt.text(3), stmt.text(4)});
    return types;
  });
}
SqliteRepository::Result<void> SqliteRepository::upsertItemType(const ItemTypeRecord &type) {
  return guarded([&] {
    valid(lexicon::validateType(type));
    Transaction tx(impl_->db, "lexicon_write");
    int id = type.id;
    if (id < 0) {
      Statement(impl_->db, "INSERT INTO item_type(group_id, name, description) VALUES(?, ?, ?);")
          .nullableId(type.groupId).bind(lexicon::trim(type.name)).bind(lexicon::trim(type.description)).run();
      id = impl_->db.lastId();
    } else {
      Statement(impl_->db, "UPDATE item_type SET group_id = ?, name = ?, description = ? WHERE id = ?;")
          .nullableId(type.groupId).bind(lexicon::trim(type.name)).bind(lexicon::trim(type.description)).bind(id).run();
      requireChanged(impl_->db, "Type");
    }
    logOperation(impl_->db, "item_type", id, type.id < 0 ? 1 : 2); tx.commit();
  });
}
SqliteRepository::Result<int> SqliteRepository::countItemsForType(int itemTypeId) {
  return guarded([&] { Statement stmt(impl_->db, "SELECT COUNT(*) FROM item WHERE item_type_id = ?;");
                        stmt.bind(itemTypeId); return stmt.step() ? stmt.integer(0) : 0; });
}
SqliteRepository::Result<void> SqliteRepository::deleteItemType(int itemTypeId) {
  return guarded([&] { Transaction tx(impl_->db, "lexicon_write");
    Statement(impl_->db, "DELETE FROM item_type WHERE id = ?;").bind(itemTypeId).run();
    requireChanged(impl_->db, "Type");
    logOperation(impl_->db, "item_type", itemTypeId, 3); tx.commit(); });
}
SqliteRepository::Result<std::vector<SqliteRepository::ItemFieldRecord>> SqliteRepository::loadItemFields(int itemTypeId) {
  return guarded([&] { return fieldsFor(impl_->db, itemTypeId); });
}
SqliteRepository::Result<void> SqliteRepository::upsertItemField(const ItemFieldRecord &field) {
  return guarded([&] {
    valid(lexicon::validateField(field));
    auto options = field.dataType == lexicon::FieldDataType::Enum
        ? lexicon::cleanedUniqueValues(field.enumOptions) : std::vector<std::string>{};
    auto json = toJsonArray(impl_->db, options);
    Transaction tx(impl_->db, "lexicon_write");
    int id = field.id;
    if (id >= 0) {
      Statement old(impl_->db, "SELECT data_type, enum_options FROM item_field WHERE id = ?;");
      old.bind(id);
      require(old.step(), "Field not found.", lexicon::Error::Code::NotFound);
      bool invalidated = old.integer(0) != static_cast<int>(field.dataType)
                      || jsonArray(impl_->db, old.text(1)) != options;
      if (invalidated)
        Statement(impl_->db, "DELETE FROM item_value WHERE item_field_id = ?;").bind(id).run();
      Statement(impl_->db, "UPDATE item_field SET name = ?, data_type = ?, position = ?, enum_options = ? WHERE id = ?;")
          .bind(lexicon::trim(field.name)).bind(static_cast<int>(field.dataType)).bind(field.position).bind(json).bind(id).run();
      requireChanged(impl_->db, "Field");
    } else {
      Statement(impl_->db, "INSERT INTO item_field(item_type_id, name, data_type, position, enum_options) VALUES(?, ?, ?, ?, ?);")
          .bind(field.itemTypeId).bind(lexicon::trim(field.name)).bind(static_cast<int>(field.dataType))
          .bind(field.position).bind(json).run();
      id = impl_->db.lastId();
    }
    logOperation(impl_->db, "item_field", id, field.id < 0 ? 1 : 2); tx.commit();
  });
}
SqliteRepository::Result<int> SqliteRepository::countFieldValues(int fieldId) {
  return guarded([&] { Statement stmt(impl_->db, "SELECT COUNT(*) FROM item_value WHERE item_field_id = ?;");
                        stmt.bind(fieldId); return stmt.step() ? stmt.integer(0) : 0; });
}
SqliteRepository::Result<void> SqliteRepository::deleteItemField(int fieldId) {
  return guarded([&] { Transaction tx(impl_->db, "lexicon_write");
    Statement(impl_->db, "DELETE FROM item_field WHERE id = ?;").bind(fieldId).run();
    requireChanged(impl_->db, "Field");
    logOperation(impl_->db, "item_field", fieldId, 3); tx.commit(); });
}

namespace {
void appendFilters(std::string &sql, int groupId, int typeId,
                   const std::vector<lexicon::ItemValueFilter> &values,
                   const std::string &searchText,
                   const lexicon::ItemColumnFilters &columns,
                   const std::vector<lexicon::ItemPropertyFilter> &properties,
                   const std::string &tag, const std::string &flag,
                   int understanding, int status, int pinned) {
  if (groupId > 0) sql += "AND t.group_id = ? ";
  if (typeId > 0) sql += "AND t.item_type_id = ? ";
  for (const auto &filter : values)
    sql += filter.exact
      ? "AND EXISTS (SELECT 1 FROM item_value iv WHERE iv.item_id = t.id AND iv.item_field_id = ? AND iv.value = ?) "
      : "AND EXISTS (SELECT 1 FROM item_value iv WHERE iv.item_id = t.id AND iv.item_field_id = ? AND INSTR(LOWER(iv.value), LOWER(?)) > 0) ";
  if (!columns.id.empty()) sql += "AND t.id = ? ";
  if (!columns.title.empty()) sql += "AND INSTR(LOWER(t.title), LOWER(?)) > 0 ";
  if (!columns.disambiguation.empty()) sql += "AND INSTR(LOWER(COALESCE(t.disambiguation, '')), LOWER(?)) > 0 ";
  if (!columns.alias.empty()) sql += "AND EXISTS (SELECT 1 FROM alias a WHERE a.item_id = t.id AND INSTR(LOWER(a.alias), LOWER(?)) > 0) ";
  for (const auto &filter : properties) {
    if (lexicon::trim(filter.key).empty()) continue;
    sql += "AND EXISTS (SELECT 1 FROM property p WHERE p.item_id = t.id AND p.\"key\" = ? COLLATE NOCASE ";
    if (!lexicon::trim(filter.value).empty()) sql += "AND INSTR(LOWER(p.value), LOWER(?)) > 0 ";
    sql += ") ";
  }
  if (understanding >= 0) sql += "AND t.understanding = ? ";
  if (status >= 0) sql += "AND t.status = ? ";
  if (pinned >= 0) sql += "AND t.pinned = ? ";
  if (!lexicon::trim(tag).empty()) sql += "AND EXISTS (SELECT 1 FROM tag tg WHERE tg.item_id = t.id AND tg.name = ?) ";
  if (!lexicon::trim(flag).empty()) sql += "AND EXISTS (SELECT 1 FROM flag fg WHERE fg.item_id = t.id AND fg.name = ?) ";
  if (!lexicon::trim(searchText).empty())
    sql += "AND (LOWER(t.title) LIKE ? OR LOWER(COALESCE(t.disambiguation, '')) LIKE ? "
           "OR EXISTS (SELECT 1 FROM alias a WHERE a.item_id = t.id AND LOWER(a.alias) LIKE ?) "
           "OR EXISTS (SELECT 1 FROM tag tg WHERE tg.item_id = t.id AND LOWER(tg.name) LIKE ?) "
           "OR EXISTS (SELECT 1 FROM flag fg WHERE fg.item_id = t.id AND LOWER(fg.name) LIKE ?)) ";
}
void bindFilters(Statement &stmt, int groupId, int typeId,
                 const std::vector<lexicon::ItemValueFilter> &values,
                 const std::string &searchText,
                 const lexicon::ItemColumnFilters &columns,
                 const std::vector<lexicon::ItemPropertyFilter> &properties,
                 const std::string &tag, const std::string &flag,
                 int understanding, int status, int pinned) {
  if (groupId > 0) stmt.bind(groupId);
  if (typeId > 0) stmt.bind(typeId);
  for (const auto &filter : values) stmt.bind(filter.fieldId).bind(filter.value);
  if (!columns.id.empty()) stmt.bind(columns.id);
  if (!columns.title.empty()) stmt.bind(columns.title);
  if (!columns.disambiguation.empty()) stmt.bind(columns.disambiguation);
  if (!columns.alias.empty()) stmt.bind(columns.alias);
  for (const auto &filter : properties) {
    if (lexicon::trim(filter.key).empty()) continue;
    stmt.bind(lexicon::trim(filter.key));
    if (!lexicon::trim(filter.value).empty()) stmt.bind(lexicon::trim(filter.value));
  }
  if (understanding >= 0) stmt.bind(understanding);
  if (status >= 0) stmt.bind(status);
  if (pinned >= 0) stmt.bind(pinned);
  if (!lexicon::trim(tag).empty()) stmt.bind(lexicon::trim(tag));
  if (!lexicon::trim(flag).empty()) stmt.bind(lexicon::trim(flag));
  if (!lexicon::trim(searchText).empty()) {
    std::string like = "%" + lexicon::asciiFold(lexicon::trim(searchText)) + "%";
    for (int i = 0; i < 5; ++i) stmt.bind(like);
  }
}
} // namespace

SqliteRepository::Result<std::vector<SqliteRepository::ItemRecord>> SqliteRepository::loadItems(
    int groupId, int typeId, const std::vector<ItemValueFilter> &valueFilters,
    const std::string &searchText, const ItemColumnFilters &columnFilters,
    const std::vector<ItemPropertyFilter> &propertyFilters,
    const std::string &tagFilter, const std::string &flagFilter,
    int understandingFilter, int statusFilter, int pinnedFilter, int limit,
    int offset, int sortColumn, SortOrder sortOrder) {
  return guarded([&] {
    std::string sql =
      "SELECT t.id, m.name, t.group_id, t.title, t.disambiguation, "
      "COALESCE((SELECT GROUP_CONCAT(a.alias, ', ') FROM alias a WHERE a.item_id = t.id), '') AS aliases, "
      "COALESCE((SELECT GROUP_CONCAT(g.name, ', ') FROM tag g WHERE g.item_id = t.id), '') AS tags, "
      "COALESCE((SELECT GROUP_CONCAT(f.name, ', ') FROM flag f WHERE f.item_id = t.id), '') AS flags, "
      "t.understanding, t.status, t.pinned, "
      "COALESCE(ty.name || CASE WHEN ty.group_id IS NULL THEN ' (All groups)' ELSE '' END, '') "
      "FROM item t JOIN item_group m ON m.id = t.group_id "
      "LEFT JOIN item_type ty ON ty.id = t.item_type_id WHERE 1 = 1 ";
    appendFilters(sql, groupId, typeId, valueFilters, searchText, columnFilters,
                  propertyFilters, tagFilter, flagFilter, understandingFilter, statusFilter, pinnedFilter);
    std::string order;
    switch (sortColumn) {
      case 0: order = "t.id"; break;
      case 1: order = "m.position"; break;
      case 2: order = "COALESCE(ty.name, '') COLLATE NOCASE"; break;
      case 3: order = "t.title COLLATE NOCASE"; break;
      case 4: order = "COALESCE(t.disambiguation, '') COLLATE NOCASE"; break;
      case 5: order = "tags COLLATE NOCASE"; break;
      case 6: order = "flags COLLATE NOCASE"; break;
      case 7: order = "aliases COLLATE NOCASE"; break;
      case 8: order = "t.status"; break;
      case 9: order = "t.understanding"; break;
      case 10: order = "t.pinned"; break;
      default: order = "t.title COLLATE NOCASE"; break;
    }
    if (sortColumn >= 11 && typeId > 0) {
      auto fields = fieldsFor(impl_->db, typeId);
      const auto index = static_cast<std::size_t>(sortColumn - 11);
      if (index < fields.size()) {
        const auto &field = fields[index];
        std::string value = "(SELECT iv.value FROM item_value iv WHERE iv.item_id = t.id AND iv.item_field_id = "
                            + std::to_string(field.id) + ")";
        order = field.dataType == lexicon::FieldDataType::Integer || field.dataType == lexicon::FieldDataType::Float
          ? "CAST(" + value + " AS REAL)" : value + " COLLATE NOCASE";
      }
    }
    sql += "ORDER BY " + order + (sortOrder == SortOrder::Ascending ? " ASC " : " DESC ");
    if (sortColumn == 1) sql += ", m.name COLLATE NOCASE";
    if (sortColumn != 3) sql += ", t.title COLLATE NOCASE";
    if (sortColumn != 4) sql += ", COALESCE(t.disambiguation, '') COLLATE NOCASE";
    sql += ", t.id ASC ";
    if (limit > 0) sql += "LIMIT ? OFFSET ? ";
    Statement stmt(impl_->db, sql);
    bindFilters(stmt, groupId, typeId, valueFilters, searchText, columnFilters,
                propertyFilters, tagFilter, flagFilter, understandingFilter, statusFilter, pinnedFilter);
    if (limit > 0) stmt.bind(limit).bind(offset);
    std::vector<ItemRecord> items;
    while (stmt.step()) {
      ItemRecord item;
      item.id = stmt.integer(0); item.groupName = stmt.text(1); item.groupId = stmt.integer(2);
      item.title = stmt.text(3); item.disambiguation = stmt.text(4);
      item.aliases = splitJoined(stmt.text(5)); item.tags = splitJoined(stmt.text(6));
      item.flags = splitJoined(stmt.text(7));
      item.understanding = static_cast<lexicon::UnderstandingLevel>(stmt.integer(8));
      item.status = static_cast<lexicon::ItemStatus>(stmt.integer(9));
      item.pinned = stmt.integer(10) != 0; item.itemTypeName = stmt.text(11);
      items.push_back(std::move(item));
    }
    if (typeId > 0 && !items.empty()) {
      std::string valuesSql = "SELECT item_id, item_field_id, value FROM item_value WHERE item_id IN (";
      for (std::size_t i = 0; i < items.size(); ++i) valuesSql += i ? ",?" : "?";
      valuesSql += ");";
      Statement values(impl_->db, valuesSql);
      std::map<int, std::size_t> rowById;
      for (std::size_t i = 0; i < items.size(); ++i) { values.bind(items[i].id); rowById[items[i].id] = i; }
      while (values.step()) items[rowById.at(values.integer(0))].fieldValues[values.integer(1)] = values.text(2);
    }
    return items;
  });
}
SqliteRepository::Result<int> SqliteRepository::countItems(
    int groupId, int typeId, const std::vector<ItemValueFilter> &valueFilters,
    const std::string &searchText, const ItemColumnFilters &columnFilters,
    const std::vector<ItemPropertyFilter> &propertyFilters,
    const std::string &tagFilter, const std::string &flagFilter,
    int understandingFilter, int statusFilter, int pinnedFilter) {
  return guarded([&] {
    std::string sql = "SELECT COUNT(*) FROM item t WHERE 1 = 1 ";
    appendFilters(sql, groupId, typeId, valueFilters, searchText, columnFilters,
                  propertyFilters, tagFilter, flagFilter, understandingFilter, statusFilter, pinnedFilter);
    Statement stmt(impl_->db, sql);
    bindFilters(stmt, groupId, typeId, valueFilters, searchText, columnFilters,
                propertyFilters, tagFilter, flagFilter, understandingFilter, statusFilter, pinnedFilter);
    return stmt.step() ? stmt.integer(0) : 0;
  });
}

SqliteRepository::Result<SqliteRepository::ItemRecord> SqliteRepository::loadItem(int itemId) {
  return guarded([&] {
    Statement stmt(impl_->db,
      "SELECT t.id, t.group_id, m.name, t.title, COALESCE(t.disambiguation, ''), "
      "t.understanding, t.status, t.pinned, COALESCE(t.content, ''), t.item_type_id, COALESCE(ty.name, '') "
      "FROM item t JOIN item_group m ON m.id = t.group_id "
      "LEFT JOIN item_type ty ON ty.id = t.item_type_id WHERE t.id = ?;");
    stmt.bind(itemId);
    require(stmt.step(), "Item not found.", lexicon::Error::Code::NotFound);
    ItemRecord item;
    item.id = stmt.integer(0); item.groupId = stmt.integer(1); item.groupName = stmt.text(2);
    item.title = stmt.text(3); item.disambiguation = stmt.text(4);
    item.understanding = static_cast<lexicon::UnderstandingLevel>(stmt.integer(5));
    item.status = static_cast<lexicon::ItemStatus>(stmt.integer(6));
    item.pinned = stmt.integer(7) != 0; item.content = stmt.text(8);
    item.itemTypeId = stmt.isNull(9) ? -1 : stmt.integer(9); item.itemTypeName = stmt.text(10);
    Statement values(impl_->db, "SELECT item_field_id, value FROM item_value WHERE item_id = ?;");
    values.bind(itemId);
    while (values.step()) item.fieldValues[values.integer(0)] = values.text(1);
    Statement properties(impl_->db, "SELECT \"key\", value FROM property WHERE item_id = ? ORDER BY \"key\" COLLATE NOCASE;");
    properties.bind(itemId);
    while (properties.step()) item.properties.push_back({properties.text(0), properties.text(1)});
    item.aliases = strings(impl_->db, "SELECT alias FROM alias WHERE item_id = ? ORDER BY alias COLLATE NOCASE;", itemId);
    item.tags = strings(impl_->db, "SELECT name FROM tag WHERE item_id = ? ORDER BY name COLLATE NOCASE;", itemId);
    item.flags = strings(impl_->db, "SELECT name FROM flag WHERE item_id = ? ORDER BY name COLLATE NOCASE;", itemId);
    return item;
  });
}

namespace {
void verifySavedBlobs(const Connection &db, const std::string &databasePath, int itemId);
int saveItemNative(const Connection &db, const std::string &databasePath,
                   const lexicon::ItemRecord &item) {
  auto fields = item.itemTypeId > 0 ? fieldsFor(db, item.itemTypeId) : std::vector<lexicon::ItemFieldRecord>{};
  valid(lexicon::validateItem(item, fields));
  Transaction tx(db, "lexicon_write");
  int id = item.id;
  if (id < 0) {
    Statement(db, "INSERT INTO item(group_id, title, disambiguation, understanding, status, pinned, content, item_type_id) "
                  "VALUES(?, ?, NULLIF(?, ''), ?, ?, ?, ?, ?);")
      .bind(item.groupId).bind(lexicon::trim(item.title)).bind(lexicon::trim(item.disambiguation))
      .bind(static_cast<int>(item.understanding)).bind(static_cast<int>(item.status)).bind(item.pinned ? 1 : 0)
      .bind(item.content).nullableId(item.itemTypeId).run();
    id = db.lastId();
  } else {
    Statement(db, "UPDATE item SET group_id = ?, title = ?, disambiguation = NULLIF(?, ''), "
                  "understanding = ?, status = ?, pinned = ?, content = ?, item_type_id = ? WHERE id = ?;")
      .bind(item.groupId).bind(lexicon::trim(item.title)).bind(lexicon::trim(item.disambiguation))
      .bind(static_cast<int>(item.understanding)).bind(static_cast<int>(item.status)).bind(item.pinned ? 1 : 0)
      .bind(item.content).nullableId(item.itemTypeId).bind(id).run();
    requireChanged(db, "Item");
  }
  replaceStrings(db, "alias", "alias", id, item.aliases);
  replaceStrings(db, "tag", "name", id, item.tags);
  replaceStrings(db, "flag", "name", id, item.flags);
  Statement(db, "DELETE FROM item_value WHERE item_id = ?;").bind(id).run();
  Statement value(db, "INSERT INTO item_value(item_id, item_field_id, value) VALUES(?, ?, ?);");
  for (const auto &[fieldId, text] : item.fieldValues) {
    value.bind(id).bind(fieldId).bind(text).run(); value.reset();
  }
  Statement(db, "DELETE FROM property WHERE item_id = ?;").bind(id).run();
  Statement property(db, "INSERT INTO property(item_id, \"key\", value) VALUES(?, ?, ?);");
  for (const auto &entry : item.properties) {
    property.bind(id).bind(lexicon::trim(entry.key)).bind(entry.value).run(); property.reset();
  }
  logOperation(db, "item", id, item.id < 0 ? 1 : 2);
  // The INSERT/UPDATE above holds SQLite's writer lock. GC cannot sweep while
  // these just-written Blob references are checked and committed.
  verifySavedBlobs(db, databasePath, id);
  tx.commit();
  return id;
}
} // namespace
SqliteRepository::Result<void> SqliteRepository::saveItem(const ItemRecord &item) {
  return guarded([&] { saveItemNative(impl_->db, impl_->path, item); });
}
SqliteRepository::Result<int> SqliteRepository::saveItemReturningId(const ItemRecord &item) {
  return guarded([&] { return saveItemNative(impl_->db, impl_->path, item); });
}
SqliteRepository::Result<void> SqliteRepository::deleteItem(int itemId) {
  return guarded([&] { Transaction tx(impl_->db, "lexicon_write");
    Statement(impl_->db, "DELETE FROM item WHERE id = ?;").bind(itemId).run();
    requireChanged(impl_->db, "Item");
    logOperation(impl_->db, "item", itemId, 3); tx.commit(); });
}
namespace {
std::vector<lexicon::LinkRecord> loadLinksNative(const Connection &db, int itemId, bool incoming) {
  Statement stmt(db, incoming
    ? "SELECT l.id, l.from_item_id, l.to_item_id, l.link_type, l.position, l.custom_value, t.title "
      "FROM link l JOIN item t ON l.from_item_id = t.id WHERE l.to_item_id = ? "
      "ORDER BY l.position, t.title COLLATE NOCASE;"
    : "SELECT l.id, l.from_item_id, l.to_item_id, l.link_type, l.position, l.custom_value, t.title "
      "FROM link l JOIN item t ON l.to_item_id = t.id WHERE l.from_item_id = ? "
      "ORDER BY l.position, t.title COLLATE NOCASE;");
  stmt.bind(itemId);
  std::vector<lexicon::LinkRecord> links;
  while (stmt.step()) {
    lexicon::LinkRecord link;
    link.id = stmt.integer(0); link.fromItemId = stmt.integer(1); link.toItemId = stmt.integer(2);
    link.linkType = static_cast<lexicon::LinkType>(stmt.integer(3));
    link.position = stmt.integer(4); link.customValue = stmt.text(5);
    if (incoming) link.fromItemTitle = stmt.text(6); else link.toItemTitle = stmt.text(6);
    links.push_back(std::move(link));
  }
  return links;
}
} // namespace
SqliteRepository::Result<std::vector<SqliteRepository::LinkRecord>> SqliteRepository::loadLinks(int itemId) {
  return guarded([&] { return loadLinksNative(impl_->db, itemId, false); });
}
SqliteRepository::Result<std::vector<SqliteRepository::LinkRecord>> SqliteRepository::loadBacklinks(int itemId) {
  return guarded([&] { return loadLinksNative(impl_->db, itemId, true); });
}
SqliteRepository::Result<void> SqliteRepository::saveLink(const LinkRecord &link) {
  return guarded([&] {
    valid(lexicon::validateLink(link));
    const std::string customValue = link.linkType == lexicon::LinkType::Custom
                                        ? lexicon::trim(link.customValue)
                                        : std::string{};
    Transaction tx(impl_->db, "lexicon_write");
    int id = link.id;
    if (id < 0) {
      Statement(impl_->db, "INSERT INTO link (from_item_id, to_item_id, link_type, position, custom_value) VALUES (?, ?, ?, ?, ?);")
        .bind(link.fromItemId).bind(link.toItemId).bind(static_cast<int>(link.linkType)).bind(link.position)
        .bind(customValue).run();
      id = impl_->db.lastId();
    } else {
      Statement(impl_->db, "UPDATE link SET from_item_id = ?, to_item_id = ?, link_type = ?, position = ?, custom_value = ? WHERE id = ?;")
        .bind(link.fromItemId).bind(link.toItemId).bind(static_cast<int>(link.linkType)).bind(link.position)
        .bind(customValue).bind(id).run();
      requireChanged(impl_->db, "Link");
    }
    logOperation(impl_->db, "link", id, link.id < 0 ? 1 : 2); tx.commit();
  });
}
SqliteRepository::Result<void> SqliteRepository::deleteLink(int linkId) {
  return guarded([&] { Transaction tx(impl_->db, "lexicon_write");
    Statement(impl_->db, "DELETE FROM link WHERE id = ?;").bind(linkId).run();
    requireChanged(impl_->db, "Link");
    logOperation(impl_->db, "link", linkId, 3); tx.commit(); });
}
SqliteRepository::Result<void> SqliteRepository::logItemRead(int itemId) {
  return guarded([&] {
    Statement(impl_->db, "INSERT INTO log(table_name, record_id, log_type) "
                         "SELECT 'item', id, 4 FROM item WHERE id = ?;").bind(itemId).run();
    requireChanged(impl_->db, "Item");
  });
}
SqliteRepository::Result<std::vector<std::string>> SqliteRepository::loadSuggestions() {
  return guarded([&] {
    Statement stmt(impl_->db, "SELECT title AS value FROM item UNION SELECT alias AS value FROM alias ORDER BY value COLLATE NOCASE;");
    std::vector<std::string> values;
    std::set<std::string> seen;
    while (stmt.step()) {
      auto value = lexicon::trim(stmt.text(0));
      if (!value.empty() && seen.insert(lexicon::asciiFold(value)).second) values.push_back(std::move(value));
    }
    return values;
  });
}
SqliteRepository::Result<std::vector<std::string>> SqliteRepository::loadItemTitles() {
  return guarded([&] {
    Statement stmt(impl_->db, "SELECT title, disambiguation FROM item ORDER BY title COLLATE NOCASE;");
    std::vector<std::string> values;
    while (stmt.step()) {
      auto title = stmt.text(0); auto disambiguation = stmt.text(1);
      values.push_back(disambiguation.empty() ? title : title + " [" + disambiguation + "]");
    }
    return values;
  });
}
namespace {
std::vector<lexicon::UsageValueRecord> loadUsage(const Connection &db, const char *sql) {
  Statement stmt(db, sql);
  std::vector<lexicon::UsageValueRecord> values;
  while (stmt.step()) values.push_back({stmt.text(0), stmt.integer(1)});
  return values;
}
} // namespace
SqliteRepository::Result<std::vector<SqliteRepository::UsageValueRecord>> SqliteRepository::loadTagUsage() {
  return guarded([&] { return loadUsage(impl_->db, "SELECT name, COUNT(*) AS usage_count FROM tag GROUP BY name ORDER BY name COLLATE NOCASE;"); });
}
SqliteRepository::Result<std::vector<SqliteRepository::UsageValueRecord>> SqliteRepository::loadFlagUsage() {
  return guarded([&] { return loadUsage(impl_->db, "SELECT name, COUNT(*) AS usage_count FROM flag GROUP BY name ORDER BY name COLLATE NOCASE;"); });
}
SqliteRepository::Result<std::vector<SqliteRepository::UsageValueRecord>> SqliteRepository::loadAliasUsage() {
  return guarded([&] { return loadUsage(impl_->db, "SELECT alias, COUNT(*) AS usage_count FROM alias GROUP BY alias ORDER BY alias COLLATE NOCASE;"); });
}
SqliteRepository::Result<int> SqliteRepository::findItemId(const std::string &title, const std::string &disambiguation) {
  return guarded([&] {
    if (disambiguation.empty()) {
      Statement exact(impl_->db, "SELECT id FROM item WHERE title = ? AND (disambiguation IS NULL OR disambiguation = '') LIMIT 1;");
      exact.bind(title);
      if (exact.step()) return exact.integer(0);
      Statement fallback(impl_->db, "SELECT id FROM item WHERE title = ? LIMIT 1;");
      fallback.bind(title);
      if (fallback.step()) return fallback.integer(0);
    } else {
      Statement exact(impl_->db, "SELECT id FROM item WHERE title = ? AND disambiguation = ? LIMIT 1;");
      exact.bind(title).bind(disambiguation);
      if (exact.step()) return exact.integer(0);
    }
    throw Failure("Item not found.", lexicon::Error::Code::NotFound);
  });
}
SqliteRepository::Result<void> SqliteRepository::beginUnitOfWork() {
  return guarded([&] {
    require(impl_->unitState == Impl::UnitState::Idle,
            "Unit of work is already active or its state is uncertain.",
            lexicon::Error::Code::Storage);
    impl_->db.exec("SAVEPOINT lexicon_unit");
    impl_->unitState = Impl::UnitState::Active;
  });
}
SqliteRepository::Result<void> SqliteRepository::commitUnitOfWork() {
  return guarded([&] {
    require(impl_->unitState == Impl::UnitState::Active,
            "No active unit of work.", lexicon::Error::Code::Storage);
    impl_->db.exec("RELEASE SAVEPOINT lexicon_unit");
    impl_->unitState = Impl::UnitState::Idle;
  });
}
SqliteRepository::Result<void> SqliteRepository::rollbackUnitOfWork() {
  return guarded([&] {
    require(impl_->unitState == Impl::UnitState::Active,
            "No active unit of work to roll back.",
            lexicon::Error::Code::Storage);
    try {
      impl_->db.exec("ROLLBACK TO SAVEPOINT lexicon_unit");
      impl_->db.exec("RELEASE SAVEPOINT lexicon_unit");
      impl_->unitState = Impl::UnitState::Idle;
    } catch (...) {
      // An unknown savepoint state must never be reused as a healthy connection.
      impl_->unitState = Impl::UnitState::Failed;
      impl_->db.close();
      throw;
    }
  });
}

namespace {
namespace fs = std::filesystem;
// The desktop uses one event loop. This also coordinates Blob operations made
// through multiple repositories in the same process.
std::mutex blobStorageMutex;
fs::path utf8Path(const std::string &value) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t *>(value.data()), value.size()));
}
std::string hexDigest(const unsigned char *bytes, unsigned length) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string text;
  text.reserve(length * 2);
  for (unsigned i = 0; i < length; ++i) {
    text += digits[bytes[i] >> 4]; text += digits[bytes[i] & 15];
  }
  return text;
}
bool validHash(const std::string &hash) {
  return hash.size() == 64 && std::all_of(hash.begin(), hash.end(), [](char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
  });
}
fs::path blobRoot(const std::string &databasePath) {
  return utf8Path(databasePath).parent_path() / "blobs";
}
fs::path blobPath(const fs::path &root, const std::string &hash) {
  return root / hash.substr(0, 2) / hash.substr(2);
}
bool hexPrefix(const std::string &prefix) {
  return prefix.size() == 2 && std::all_of(prefix.begin(), prefix.end(), [](char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
  });
}
fs::file_status noFollow(const fs::path &path) {
  std::error_code error;
  auto status = fs::symlink_status(path, error);
  if (error && error != std::errc::no_such_file_or_directory)
    throw Failure("Cannot inspect Blob path: " + error.message());
  return status;
}
std::string hashFile(const fs::path &path,
                     const std::function<void(const char *, std::size_t)> &copy = {}) {
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> digest(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!digest || EVP_DigestInit_ex(digest.get(), EVP_sha256(), nullptr) != 1)
    throw Failure("Cannot initialize SHA-256.");
  // On the heap: a megabyte does not fit on a thread stack everywhere. Windows
  // reserves one or two for a thread, and importBlob below is already holding
  // a buffer of its own further down the same stack.
  std::vector<char> storage(1024 * 1024);
  char *buffer = storage.data();
  const std::size_t bufferSize = storage.size();
#ifdef _WIN32
  HANDLE descriptor = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                  nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
                                  nullptr);
  if (descriptor == INVALID_HANDLE_VALUE) throw Failure("Cannot open Blob file.");
  struct Handle {
    HANDLE value;
    ~Handle() { CloseHandle(value); }
  } handle{descriptor};
  BY_HANDLE_FILE_INFORMATION metadata{};
  if (!GetFileInformationByHandle(descriptor, &metadata) ||
      (metadata.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
    throw Failure("Blob path is not a regular file.");
  for (;;) {
    DWORD count = 0;
    if (!ReadFile(descriptor, buffer, static_cast<DWORD>(bufferSize), &count,
                  nullptr))
      throw Failure("Cannot read Blob file completely.");
    if (count == 0) break;
    if (EVP_DigestUpdate(digest.get(), buffer, static_cast<std::size_t>(count)) != 1)
      throw Failure("Cannot hash Blob file.");
    if (copy) copy(buffer, static_cast<std::size_t>(count));
  }
#else
  int descriptor = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (descriptor < 0) throw Failure("Cannot open regular Blob file: " + std::to_string(errno));
  struct Descriptor {
    int value;
    ~Descriptor() { ::close(value); }
  } handle{descriptor};
  struct stat metadata {};
  if (::fstat(descriptor, &metadata) != 0 || !S_ISREG(metadata.st_mode))
    throw Failure("Blob path is not a regular file.");
  for (;;) {
    const auto count = ::read(descriptor, buffer, bufferSize);
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) throw Failure("Cannot read Blob file completely.");
    if (count == 0) break;
    if (EVP_DigestUpdate(digest.get(), buffer, static_cast<std::size_t>(count)) != 1)
      throw Failure("Cannot hash Blob file.");
    if (copy) copy(buffer, static_cast<std::size_t>(count));
  }
#endif
  unsigned char bytes[EVP_MAX_MD_SIZE]; unsigned length = 0;
  if (EVP_DigestFinal_ex(digest.get(), bytes, &length) != 1)
    throw Failure("Cannot finish SHA-256.");
  return hexDigest(bytes, length);
}
void requireSafeRoot(const fs::path &root) {
  auto status = noFollow(root);
  require(status.type() == fs::file_type::directory, "Blob root is not a directory.",
          lexicon::Error::Code::Storage);
}
void requireSafePrefix(const fs::path &root, const std::string &hash) {
  requireSafeRoot(root);
  require(noFollow(root / hash.substr(0, 2)).type() == fs::file_type::directory,
          "Blob prefix is not a directory.", lexicon::Error::Code::Storage);
}
void verifySavedBlobs(const Connection &db, const std::string &databasePath, int itemId) {
  Statement values(db, "SELECT DISTINCT iv.value FROM item_value iv "
                       "JOIN item_field f ON f.id = iv.item_field_id "
                       "JOIN item i ON i.id = iv.item_id AND i.item_type_id = f.item_type_id "
                       "WHERE iv.item_id = ? AND f.data_type = 8;");
  values.bind(itemId);
  const auto root = blobRoot(databasePath);
  while (values.step()) {
    const auto hash = values.text(0);
    require(validHash(hash), "Item contains an invalid Blob identifier.");
    const auto prefix = root / hash.substr(0, 2);
    const auto path = blobPath(root, hash);
    const auto rootType = noFollow(root).type();
    if (rootType == fs::file_type::not_found)
      throw Failure("Referenced Blob file is missing: " + hash,
                    lexicon::Error::Code::NotFound);
    require(rootType == fs::file_type::directory, "Blob root is not a real directory.",
            lexicon::Error::Code::Storage);
    const auto prefixType = noFollow(prefix).type();
    if (prefixType == fs::file_type::not_found)
      throw Failure("Referenced Blob file is missing: " + hash,
                    lexicon::Error::Code::NotFound);
    require(prefixType == fs::file_type::directory, "Blob prefix is not a real directory.",
            lexicon::Error::Code::Storage);
    const auto fileType = noFollow(path).type();
    if (fileType == fs::file_type::not_found)
      throw Failure("Referenced Blob file is missing: " + hash,
                    lexicon::Error::Code::NotFound);
    require(fileType == fs::file_type::regular, "Blob path is not a regular file.",
            lexicon::Error::Code::Storage);
    require(hashFile(path) == hash, "Blob contents do not match its SHA-256 identifier: " + hash,
            lexicon::Error::Code::Storage);
  }
}
struct LiveBlobs {
  std::set<std::string> hashes;
  std::set<std::string> invalid;
  std::size_t references = 0;
};
LiveBlobs liveBlobs(const Connection &db) {
  LiveBlobs live;
  Statement values(db, "SELECT iv.value FROM item_value iv "
                       "JOIN item_field f ON f.id = iv.item_field_id "
                       "JOIN item i ON i.id = iv.item_id AND i.item_type_id = f.item_type_id "
                       "WHERE f.data_type = 8;");
  while (values.step()) {
    auto hash = values.text(0);
    ++live.references;
    if (validHash(hash)) live.hashes.insert(std::move(hash));
    else live.invalid.insert(std::move(hash));
  }
  return live;
}
lexicon::BlobMaintenanceReport scanBlobs(const Connection &db, const fs::path &root,
                                         lexicon::BlobScanDepth depth) {
  using lexicon::BlobIssueType;
  lexicon::BlobMaintenanceReport report;
  report.depth = depth;
  auto live = liveBlobs(db);
  report.referenceCount = live.references;
  report.referencedBlobCount = live.hashes.size();
  for (const auto &hash : live.invalid)
    report.issues.push_back({BlobIssueType::InvalidReference, hash, {}, 0,
                             "Blob field contains a noncanonical SHA-256 value."});
  std::set<std::string> present;
  auto rootStatus = noFollow(root);
  if (rootStatus.type() != fs::file_type::not_found) {
    if (rootStatus.type() != fs::file_type::directory) {
      report.issues.push_back({BlobIssueType::UnexpectedFile, {}, "blobs", 0,
                               "Blob root is not a real directory."});
    } else {
      for (const auto &prefixEntry : fs::directory_iterator(root)) {
        const auto prefix = prefixEntry.path().filename().string();
        const auto prefixStatus = noFollow(prefixEntry.path());
        if (!hexPrefix(prefix) || prefixStatus.type() != fs::file_type::directory) {
          report.issues.push_back({BlobIssueType::UnexpectedFile, {}, prefix, 0,
                                   "Unexpected entry in Blob root."});
          continue;
        }
        for (const auto &entry : fs::directory_iterator(prefixEntry.path())) {
          const auto name = entry.path().filename().string();
          const auto relative = prefix + "/" + name;
          const auto status = noFollow(entry.path());
          const auto hash = prefix + name;
          if (status.type() != fs::file_type::regular || !validHash(hash) || name.size() != 62) {
            report.issues.push_back({BlobIssueType::UnexpectedFile, {}, relative, 0,
                                     "Unexpected Blob entry; it will never be collected automatically."});
            continue;
          }
          const auto size = fs::file_size(entry.path());
          ++report.physicalBlobCount;
          report.totalBytes += size;
          present.insert(hash);
          if (depth == lexicon::BlobScanDepth::FullIntegrity && hashFile(entry.path()) != hash) {
            ++report.corruptedBlobCount;
            report.issues.push_back({BlobIssueType::HashMismatch, hash, relative, size,
                                     "File contents do not match the SHA-256 path."});
          } else if (!live.hashes.contains(hash)) {
            ++report.orphanedBlobCount;
            report.orphanedBytes += size;
            report.issues.push_back({BlobIssueType::Orphaned, hash, relative, size,
                                     depth == lexicon::BlobScanDepth::Structural
                                       ? "Unreferenced; content has not been checked yet."
                                       : "Unreferenced canonical Blob."});
          }
        }
      }
    }
  }
  for (const auto &hash : live.hashes) {
    if (!present.contains(hash)) {
      ++report.missingBlobCount;
      report.issues.push_back({BlobIssueType::Missing, hash,
                               hash.substr(0, 2) + "/" + hash.substr(2), 0,
                               "Referenced Blob file is missing."});
    }
  }
  return report;
}
struct TemporaryFile {
  fs::path path;
#ifdef _WIN32
  HANDLE descriptor = INVALID_HANDLE_VALUE;
#else
  int descriptor = -1;
#endif
  explicit TemporaryFile(const fs::path &directory) {
    const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto nonce = std::random_device{}();
    for (int i = 0; i < 100; ++i) {
      auto candidate = directory / (".lexicon-" + std::to_string(seed) + "-" +
                                    std::to_string(nonce) + "-" + std::to_string(i));
#ifdef _WIN32
      descriptor = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (descriptor == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) continue;
        throw Failure("Cannot exclusively create temporary file.");
      }
#else
      descriptor = ::open(candidate.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
      if (descriptor < 0) {
        if (errno == EEXIST) continue;
        throw Failure("Cannot exclusively create temporary file: " + std::to_string(errno));
      }
#endif
      path = std::move(candidate);
      return;
    }
    throw Failure("Cannot allocate temporary file path.");
  }
  TemporaryFile(const TemporaryFile &) = delete;
  TemporaryFile &operator=(const TemporaryFile &) = delete;
  ~TemporaryFile() {
#ifdef _WIN32
    if (descriptor != INVALID_HANDLE_VALUE) CloseHandle(descriptor);
#else
    if (descriptor >= 0) ::close(descriptor);
#endif
    if (!path.empty()) { std::error_code ignored; fs::remove(path, ignored); }
  }
  void write(const char *bytes, std::size_t length) {
    while (length != 0) {
#ifdef _WIN32
      DWORD count = 0;
      const auto requested = static_cast<DWORD>(std::min<std::size_t>(length, 1024 * 1024));
      if (!WriteFile(descriptor, bytes, requested, &count, nullptr) || count == 0)
        throw Failure("Cannot write temporary file.");
#else
      const auto count = ::write(descriptor, bytes, length);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) throw Failure("Cannot write temporary file.");
#endif
      bytes += count;
      length -= static_cast<std::size_t>(count);
    }
  }
  void close() {
#ifdef _WIN32
    if (descriptor == INVALID_HANDLE_VALUE) return;
    const bool flushed = FlushFileBuffers(descriptor) != 0;
    const bool closed = CloseHandle(descriptor) != 0;
    descriptor = INVALID_HANDLE_VALUE;
#else
    if (descriptor < 0) return;
    const bool flushed = ::fsync(descriptor) == 0;
    const bool closed = ::close(descriptor) == 0;
    descriptor = -1;
#endif
    if (!flushed || !closed) throw Failure("Cannot finish temporary file.");
  }
  void keep() { path.clear(); }
};
} // namespace
SqliteRepository::Result<std::string> SqliteRepository::importBlob(const std::string &sourcePath) {
  return guarded([&] {
    std::lock_guard lock(blobStorageMutex);
    impl_->db.get();
    fs::path root = blobRoot(impl_->path);
    fs::create_directories(root);
    requireSafeRoot(root);
    std::ifstream source(utf8Path(sourcePath), std::ios::binary);
    if (!source) throw Failure("Cannot read file.");
    TemporaryFile temp(root);
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> digest(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!digest || EVP_DigestInit_ex(digest.get(), EVP_sha256(), nullptr) != 1)
      throw Failure("Cannot initialize SHA-256.");
    // Heap again: this frame and hashFile's below it would otherwise want two
    // megabytes of stack between them.
    std::vector<char> storage(1024 * 1024);
    char *buffer = storage.data();
    while (source) {
      source.read(buffer, static_cast<std::streamsize>(storage.size()));
      auto count = source.gcount();
      if (count > 0) {
        if (EVP_DigestUpdate(digest.get(), buffer, static_cast<std::size_t>(count)) != 1)
          throw Failure("Cannot hash blob.");
        temp.write(buffer, static_cast<std::size_t>(count));
      }
    }
    if (!source.eof()) throw Failure("Cannot read file completely.");
    temp.close();
    unsigned char bytes[EVP_MAX_MD_SIZE]; unsigned length = 0;
    if (EVP_DigestFinal_ex(digest.get(), bytes, &length) != 1) throw Failure("Cannot finish SHA-256.");
    auto hash = hexDigest(bytes, length);
    // The temporary file is complete and checked before it can acquire its
    // canonical name. The same hash may be used by any number of Item values.
    require(hashFile(temp.path) == hash, "Imported Blob changed while being written.",
            lexicon::Error::Code::Storage);
    fs::path target = blobPath(root, hash);
    fs::create_directories(target.parent_path());
    requireSafePrefix(root, hash);
    if (noFollow(target).type() == fs::file_type::not_found) {
#ifdef _WIN32
      // MoveFileEx without REPLACE_EXISTING installs the complete file once.
      if (MoveFileExW(temp.path.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
        temp.keep();
      else if (noFollow(target).type() == fs::file_type::not_found)
        throw Failure("Cannot install Blob file.");
#else
      try {
        // Link creation is atomic and will never replace an existing Blob.
        fs::create_hard_link(temp.path, target);
      } catch (const fs::filesystem_error &) {
        if (noFollow(target).type() == fs::file_type::not_found) throw;
      }
#endif
    }
    require(noFollow(target).type() == fs::file_type::regular && hashFile(target) == hash,
            "Existing Blob has unexpected contents or type.", lexicon::Error::Code::Storage);
    return hash;
  });
}
SqliteRepository::Result<void> SqliteRepository::exportBlob(const std::string &hash, const std::string &destinationPath) {
  return guarded([&] {
    std::lock_guard lock(blobStorageMutex);
    impl_->db.get();
    require(validHash(hash), "Invalid blob identifier.");
    const auto root = blobRoot(impl_->path);
    if (noFollow(root).type() == fs::file_type::not_found ||
        noFollow(root / hash.substr(0, 2)).type() == fs::file_type::not_found)
      throw Failure("Referenced Blob file is missing.", lexicon::Error::Code::NotFound);
    requireSafePrefix(root, hash);
    fs::path sourcePath = blobPath(root, hash);
    require(noFollow(sourcePath).type() == fs::file_type::regular,
            "Referenced Blob file is missing.", lexicon::Error::Code::NotFound);
    fs::path target = utf8Path(destinationPath);
    TemporaryFile temp(target.parent_path());
    require(hashFile(sourcePath, [&](const char *bytes, std::size_t count) {
              temp.write(bytes, count);
            }) == hash,
            "Blob contents do not match its SHA-256 identifier.",
            lexicon::Error::Code::Storage);
    temp.close();
#ifdef _WIN32
    if (!MoveFileExW(temp.path.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      throw Failure("Cannot replace output file.");
#else
    fs::rename(temp.path, target);
#endif
    temp.keep();
  });
}

SqliteRepository::Result<lexicon::BlobMaintenanceReport>
SqliteRepository::scanBlobStorage(lexicon::BlobScanDepth depth) {
  return guarded([&] {
    std::lock_guard lock(blobStorageMutex);
    return scanBlobs(impl_->db, blobRoot(impl_->path), depth);
  });
}

SqliteRepository::Result<lexicon::BlobIssue>
SqliteRepository::verifyBlob(const std::string &hash) {
  return guarded([&] {
    std::lock_guard lock(blobStorageMutex);
    require(validHash(hash), "Invalid Blob identifier.");
    auto root = blobRoot(impl_->path);
    auto path = blobPath(root, hash);
    const auto relative = hash.substr(0, 2) + "/" + hash.substr(2);
    const auto rootType = noFollow(root).type();
    if (rootType == fs::file_type::not_found)
      return lexicon::BlobIssue{lexicon::BlobIssueType::Missing, hash, relative, 0,
                                "Blob file is missing."};
    if (rootType != fs::file_type::directory)
      return lexicon::BlobIssue{lexicon::BlobIssueType::UnexpectedFile, hash, "blobs", 0,
                                "Blob root is not a real directory."};
    const auto prefixType = noFollow(path.parent_path()).type();
    if (prefixType == fs::file_type::not_found)
      return lexicon::BlobIssue{lexicon::BlobIssueType::Missing, hash, relative, 0,
                                "Blob file is missing."};
    if (prefixType != fs::file_type::directory)
      return lexicon::BlobIssue{lexicon::BlobIssueType::UnexpectedFile, hash, relative, 0,
                                "Blob prefix is not a real directory."};
    const auto fileType = noFollow(path).type();
    if (fileType == fs::file_type::not_found)
      return lexicon::BlobIssue{lexicon::BlobIssueType::Missing, hash, relative, 0,
                                "Blob file is missing."};
    if (fileType != fs::file_type::regular)
      return lexicon::BlobIssue{lexicon::BlobIssueType::UnexpectedFile, hash, relative, 0,
                                "Blob path is not a regular file."};
    auto size = fs::file_size(path);
    if (hashFile(path) != hash)
      return lexicon::BlobIssue{lexicon::BlobIssueType::HashMismatch, hash, relative, size,
                                "Blob contents do not match the SHA-256 path."};
    return lexicon::BlobIssue{lexicon::BlobIssueType::Healthy, hash, relative, size,
                              "Blob contents match the SHA-256 path."};
  });
}

SqliteRepository::Result<lexicon::BlobGarbageCollectionResult>
SqliteRepository::collectUnusedBlobs(const lexicon::BlobMaintenanceReport &scan) {
  return guarded([&] {
    std::lock_guard lock(blobStorageMutex);
    lexicon::BlobGarbageCollectionResult result;
    const auto root = blobRoot(impl_->path);
    std::set<std::string> candidates;
    for (const auto &issue : scan.issues)
      if (issue.type == lexicon::BlobIssueType::Orphaned && validHash(issue.hash))
        candidates.insert(issue.hash);
    result.candidates = candidates.size();
    if (candidates.empty()) return result;
    // A write transaction excludes other SQLite writers from the live-set
    // check through the filesystem sweep, including writers in other processes.
    impl_->db.exec("BEGIN IMMEDIATE;");
    struct Rollback {
      const Connection &db;
      ~Rollback() { try { db.exec("ROLLBACK;"); } catch (...) {} }
    } rollback{impl_->db};
    auto live = liveBlobs(impl_->db);
    for (const auto &hash : candidates) {
      const auto relative = hash.substr(0, 2) + "/" + hash.substr(2);
      if (live.hashes.contains(hash)) { ++result.skipped; continue; }
      const auto path = blobPath(root, hash);
      try {
        if (noFollow(root).type() != fs::file_type::directory ||
            noFollow(path.parent_path()).type() != fs::file_type::directory ||
            noFollow(path).type() != fs::file_type::regular) {
          ++result.skipped;
          result.issues.push_back({lexicon::BlobIssueType::UnexpectedFile, hash, relative, 0,
                                   "Candidate is no longer a regular canonical Blob."});
          continue;
        }
        const auto size = fs::file_size(path);
        if (hashFile(path) != hash) {
          ++result.skipped;
          result.issues.push_back({lexicon::BlobIssueType::HashMismatch, hash, relative, size,
                                   "Candidate failed SHA-256 revalidation."});
          continue;
        }
        // Keep physical files when Item values disappear. Only this explicit
        // operation may remove a still-unreferenced canonical Blob.
        std::error_code error;
        if (!fs::remove(path, error) || error) {
          ++result.failed;
          result.issues.push_back({lexicon::BlobIssueType::Orphaned, hash, relative, size,
                                   error ? error.message() : "Deletion did not remove the file."});
        } else {
          ++result.deleted;
          result.deletedBytes += size;
        }
      } catch (const std::exception &error) {
        ++result.failed;
        result.issues.push_back({lexicon::BlobIssueType::Orphaned, hash, relative, 0, error.what()});
      }
    }
    return result;
  });
}
