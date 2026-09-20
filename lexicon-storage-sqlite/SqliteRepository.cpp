#include "SqliteRepository.h"
#include "BlobStore.h"
#include "Conversions.h"
#include "DatabaseManager.h"

#include <QSqlError>
#include <QSqlQuery>

#include <utility>

namespace {
lexicon::Error storageError(const QString &message) {
  return {lexicon::Error::Code::Storage,
          qtbridge::toCore(message.isEmpty()
                               ? QStringLiteral("SQLite operation failed.")
                               : message)};
}

template <class F> auto read(F &&operation) {
  using QtValue = decltype(operation(static_cast<QString *>(nullptr)));
  using CoreValue = decltype(qtbridge::toCore(std::declval<QtValue>()));
  QString error;
  QtValue value = operation(&error);
  if (!error.isEmpty())
    return lexicon::Result<CoreValue>(std::unexpected(storageError(error)));
  return lexicon::Result<CoreValue>(qtbridge::toCore(value));
}

template <class F> lexicon::Result<void> write(F &&operation) {
  QString error;
  if (!operation(&error))
    return std::unexpected(storageError(error));
  return {};
}
} // namespace

lexicon::Result<void> SqliteRepository::open(const std::string &utf8Path) {
  return write([&](QString *error) {
    return DatabaseManager::initialize(qtbridge::toQt(utf8Path), error);
  });
}
SqliteRepository::Result<std::map<std::string, std::string>>
SqliteRepository::loadConfiguration() {
  return read(
      [](QString *error) { return DatabaseManager::loadConfiguration(error); });
}
SqliteRepository::Result<void> SqliteRepository::saveConfiguration(
    const std::map<std::string, std::string> &values) {
  return write([&](QString *error) {
    return DatabaseManager::saveConfiguration(qtbridge::toQt(values), error);
  });
}
SqliteRepository::Result<std::vector<SqliteRepository::GroupRecord>>
SqliteRepository::loadGroups() {
  return read(
      [](QString *error) { return DatabaseManager::loadGroups(error); });
}
SqliteRepository::Result<int> SqliteRepository::defaultGroupId() {
  return read(
      [](QString *error) { return DatabaseManager::defaultGroupId(error); });
}
SqliteRepository::Result<void>
SqliteRepository::upsertGroup(const GroupRecord &group) {
  return write([&](QString *error) {
    return DatabaseManager::upsertGroup(qtbridge::toQt(group), error);
  });
}
SqliteRepository::Result<void> SqliteRepository::deleteGroup(int groupId) {
  return write([&](QString *error) {
    return DatabaseManager::deleteGroup(groupId, error);
  });
}
SqliteRepository::Result<std::vector<SqliteRepository::ItemTypeRecord>>
SqliteRepository::loadItemTypes(int groupId) {
  return read([&](QString *error) {
    return DatabaseManager::loadItemTypes(groupId, error);
  });
}
SqliteRepository::Result<void>
SqliteRepository::upsertItemType(const ItemTypeRecord &itemType) {
  return write([&](QString *error) {
    return DatabaseManager::upsertItemType(qtbridge::toQt(itemType), error);
  });
}
SqliteRepository::Result<int>
SqliteRepository::countItemsForType(int itemTypeId) {
  return read([&](QString *error) {
    return DatabaseManager::countItemsForType(itemTypeId, error);
  });
}
SqliteRepository::Result<void>
SqliteRepository::deleteItemType(int itemTypeId) {
  return write([&](QString *error) {
    return DatabaseManager::deleteItemType(itemTypeId, error);
  });
}
SqliteRepository::Result<std::vector<SqliteRepository::ItemFieldRecord>>
SqliteRepository::loadItemFields(int itemTypeId) {
  return read([&](QString *error) {
    return DatabaseManager::loadItemFields(itemTypeId, error);
  });
}
SqliteRepository::Result<void>
SqliteRepository::upsertItemField(const ItemFieldRecord &field) {
  return write([&](QString *error) {
    return DatabaseManager::upsertItemField(qtbridge::toQt(field), error);
  });
}
SqliteRepository::Result<int> SqliteRepository::countFieldValues(int fieldId) {
  return read([&](QString *error) {
    return DatabaseManager::countFieldValues(fieldId, error);
  });
}
SqliteRepository::Result<void> SqliteRepository::deleteItemField(int fieldId) {
  return write([&](QString *error) {
    return DatabaseManager::deleteItemField(fieldId, error);
  });
}
SqliteRepository::Result<std::vector<SqliteRepository::ItemRecord>>
SqliteRepository::loadItems(
    int groupId, int typeId, const std::vector<ItemValueFilter> &valueFilters,
    const std::string &searchText, const ItemColumnFilters &columnFilters,
    const std::vector<ItemPropertyFilter> &propertyFilters,
    const std::string &tagFilter, const std::string &flagFilter,
    int understandingFilter, int statusFilter, int pinnedFilter, int limit,
    int offset, int sortColumn, SortOrder sortOrder) {
  return read([&](QString *error) {
    return DatabaseManager::loadItems(
        groupId, typeId, qtbridge::toQt(valueFilters),
        qtbridge::toQt(searchText), qtbridge::toQt(columnFilters),
        qtbridge::toQt(propertyFilters), qtbridge::toQt(tagFilter),
        qtbridge::toQt(flagFilter), understandingFilter, statusFilter,
        pinnedFilter, limit, offset, sortColumn, qtbridge::toQt(sortOrder),
        error);
  });
}
SqliteRepository::Result<int> SqliteRepository::countItems(
    int groupId, int typeId, const std::vector<ItemValueFilter> &valueFilters,
    const std::string &searchText, const ItemColumnFilters &columnFilters,
    const std::vector<ItemPropertyFilter> &propertyFilters,
    const std::string &tagFilter, const std::string &flagFilter,
    int understandingFilter, int statusFilter, int pinnedFilter) {
  return read([&](QString *error) {
    return DatabaseManager::countItems(
        groupId, typeId, qtbridge::toQt(valueFilters),
        qtbridge::toQt(searchText), qtbridge::toQt(columnFilters),
        qtbridge::toQt(propertyFilters), qtbridge::toQt(tagFilter),
        qtbridge::toQt(flagFilter), understandingFilter, statusFilter,
        pinnedFilter, error);
  });
}
SqliteRepository::Result<SqliteRepository::ItemRecord>
SqliteRepository::loadItem(int itemId) {
  QString error;
  ::ItemRecord item;
  if (!DatabaseManager::loadItem(itemId, item, &error)) {
    return std::unexpected(lexicon::Error{error == "Item not found."
                                              ? lexicon::Error::Code::NotFound
                                              : lexicon::Error::Code::Storage,
                                          qtbridge::toCore(error)});
  }
  return qtbridge::toCore(item);
}
SqliteRepository::Result<void>
SqliteRepository::saveItem(const ItemRecord &item) {
  return write([&](QString *error) {
    return DatabaseManager::saveItem(qtbridge::toQt(item), error);
  });
}
SqliteRepository::Result<int>
SqliteRepository::saveItemReturningId(const ItemRecord &item) {
  QString error;
  int id = -1;
  if (!DatabaseManager::saveItem(qtbridge::toQt(item), &error, &id))
    return std::unexpected(storageError(error));
  return id;
}
SqliteRepository::Result<void> SqliteRepository::deleteItem(int itemId) {
  return write([&](QString *error) {
    return DatabaseManager::deleteItem(itemId, error);
  });
}
SqliteRepository::Result<std::vector<SqliteRepository::LinkRecord>>
SqliteRepository::loadLinks(int itemId) {
  return read([&](QString *error) {
    return DatabaseManager::loadLinks(itemId, error);
  });
}
SqliteRepository::Result<std::vector<SqliteRepository::LinkRecord>>
SqliteRepository::loadBacklinks(int itemId) {
  return read([&](QString *error) {
    return DatabaseManager::loadBacklinks(itemId, error);
  });
}
SqliteRepository::Result<void>
SqliteRepository::saveLink(const LinkRecord &link) {
  return write([&](QString *error) {
    return DatabaseManager::saveLink(qtbridge::toQt(link), error);
  });
}
SqliteRepository::Result<void> SqliteRepository::deleteLink(int linkId) {
  return write([&](QString *error) {
    return DatabaseManager::deleteLink(linkId, error);
  });
}
SqliteRepository::Result<void> SqliteRepository::logItemRead(int itemId) {
  return write([&](QString *error) {
    return DatabaseManager::logItemRead(itemId, error);
  });
}
SqliteRepository::Result<std::vector<std::string>>
SqliteRepository::loadSuggestions() {
  return read(
      [](QString *error) { return DatabaseManager::loadSuggestions(error); });
}
SqliteRepository::Result<std::vector<std::string>>
SqliteRepository::loadItemTitles() {
  return read(
      [](QString *error) { return DatabaseManager::loadItemTitles(error); });
}
SqliteRepository::Result<std::vector<SqliteRepository::UsageValueRecord>>
SqliteRepository::loadTagUsage() {
  return read(
      [](QString *error) { return DatabaseManager::loadTagUsage(error); });
}
SqliteRepository::Result<std::vector<SqliteRepository::UsageValueRecord>>
SqliteRepository::loadFlagUsage() {
  return read(
      [](QString *error) { return DatabaseManager::loadFlagUsage(error); });
}
SqliteRepository::Result<std::vector<SqliteRepository::UsageValueRecord>>
SqliteRepository::loadAliasUsage() {
  return read(
      [](QString *error) { return DatabaseManager::loadAliasUsage(error); });
}
SqliteRepository::Result<int>
SqliteRepository::findItemId(const std::string &title,
                             const std::string &disambiguation) {
  QSqlQuery query(DatabaseManager::database());
  const QString qtTitle = qtbridge::toQt(title);
  const QString qtDisambiguation = qtbridge::toQt(disambiguation);
  if (qtDisambiguation.isEmpty()) {
    query.prepare("SELECT id FROM item WHERE title = ? AND (disambiguation IS "
                  "NULL OR disambiguation = '') LIMIT 1");
    query.addBindValue(qtTitle);
    if (!query.exec())
      return std::unexpected(storageError(query.lastError().text()));
    if (query.next())
      return query.value(0).toInt();
    query.prepare("SELECT id FROM item WHERE title = ? LIMIT 1");
    query.addBindValue(qtTitle);
  } else {
    query.prepare(
        "SELECT id FROM item WHERE title = ? AND disambiguation = ? LIMIT 1");
    query.addBindValue(qtTitle);
    query.addBindValue(qtDisambiguation);
  }
  if (!query.exec())
    return std::unexpected(storageError(query.lastError().text()));
  if (query.next())
    return query.value(0).toInt();
  return std::unexpected(
      lexicon::Error{lexicon::Error::Code::NotFound, "Item not found."});
}
SqliteRepository::Result<void> SqliteRepository::beginUnitOfWork() {
  QSqlQuery query(DatabaseManager::database());
  if (!query.exec("SAVEPOINT lexicon_unit"))
    return std::unexpected(storageError(query.lastError().text()));
  return {};
}
SqliteRepository::Result<void> SqliteRepository::commitUnitOfWork() {
  QSqlQuery query(DatabaseManager::database());
  if (!query.exec("RELEASE SAVEPOINT lexicon_unit"))
    return std::unexpected(storageError(query.lastError().text()));
  return {};
}
void SqliteRepository::rollbackUnitOfWork() {
  QSqlQuery query(DatabaseManager::database());
  query.exec("ROLLBACK TO SAVEPOINT lexicon_unit");
  query.exec("RELEASE SAVEPOINT lexicon_unit");
}
SqliteRepository::Result<std::string>
SqliteRepository::importBlob(const std::string &sourcePath) {
  return read([&](QString *error) {
    return BlobStore::importFile(qtbridge::toQt(sourcePath), error);
  });
}
SqliteRepository::Result<void>
SqliteRepository::exportBlob(const std::string &hash,
                             const std::string &destinationPath) {
  return write([&](QString *error) {
    return BlobStore::exportFile(qtbridge::toQt(hash),
                                 qtbridge::toQt(destinationPath), error);
  });
}
