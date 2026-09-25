#include "LexiconApplication.h"
#include "SqliteRepository.h"
#include "Validation.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>

namespace {
namespace fs = std::filesystem;
bool expect(bool value, const char *message) {
  if (!value) std::cerr << message << '\n';
  return value;
}
template <class T> bool success(const lexicon::Result<T> &result, const char *message) {
  if (result) return true;
  std::cerr << message << ": " << result.error().message << '\n';
  return false;
}
bool fts5Available() {
  sqlite3 *db = nullptr;
  if (sqlite3_open(":memory:", &db) != SQLITE_OK) return false;
  const bool available =
      sqlite3_exec(db, "CREATE VIRTUAL TABLE probe USING fts5(x);", nullptr, nullptr, nullptr) == SQLITE_OK;
  sqlite3_close(db);
  return available;
}
struct TemporaryDirectory {
  fs::path path;
  TemporaryDirectory() {
    path = fs::temp_directory_path() /
      ("lexicon-compat-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(path);
  }
  ~TemporaryDirectory() { std::error_code ec; fs::remove_all(path, ec); }
};
}
int main() {
  TemporaryDirectory temp;
  const auto fixture = fs::path(LEXICON_FIXTURE_DIR);
  auto dbPath = temp.path / "lexicon.db";
  fs::copy_file(fixture / "qt-v10.db", dbPath);
  {
    SqliteRepository repository;
    if (!success(repository.open(dbPath.string()), "Migrate Qt v10 database")) return 1;
    lexicon::LexiconApplication app(repository);
    auto groups = app.groups.loadGroups();
    if (!success(groups, "Load migrated groups")) return 1;
    auto defaultGroup = app.groups.defaultGroupId();
    if (!success(defaultGroup, "Resolve migrated Default") ||
        !expect(std::any_of(groups->begin(), groups->end(),
                            [&](const auto &group) {
                              return group.id == *defaultGroup && group.name == "Default";
                            }), "Migration did not preserve Default")) return 1;
    auto found = app.search.findItemId("Žluťoučký kůň", "");
    if (!success(found, "Find migrated UTF-8 term")) return 1;
    auto item = app.items.loadItem(*found);
    if (!success(item, "Load migrated term") ||
        !expect(item->content == "Dřívější záznam" && item->aliases == std::vector<std::string>{"kůň"},
                "Migrated content changed")) return 1;
    item->title = "Historický kůň";
    if (!success(app.items.saveItem(*item), "Update migrated item")) return 1;
    auto updated = app.items.loadItem(*found);
    if (!success(updated, "Reload migrated item") ||
        !expect(updated->title == item->title, "Migrated database is not writable")) return 1;
  }
  fs::remove(dbPath);
  fs::copy_file(fixture / "qt-v20.db", dbPath);
  // Migration 24 rebuilds item_field; its IDs, AUTOINCREMENT sequence and
  // the values that refer to it must come through.
  const auto rawQuery = [&dbPath](const std::string &sql) {
    sqlite3 *raw = nullptr;
    std::string result;
    if (sqlite3_open(dbPath.string().c_str(), &raw) == SQLITE_OK) {
      sqlite3_stmt *statement = nullptr;
      if (sqlite3_prepare_v2(raw, sql.c_str(), -1, &statement, nullptr) == SQLITE_OK &&
          sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_text(statement, 0))
        result = reinterpret_cast<const char *>(sqlite3_column_text(statement, 0));
      sqlite3_finalize(statement);
    }
    sqlite3_close(raw);
    return result;
  };
  const auto fieldsBefore = rawQuery("SELECT group_concat(id || ':' || name, ',') FROM (SELECT * FROM item_field ORDER BY id);");
  const auto valuesBefore = rawQuery("SELECT COUNT(*) FROM item_value;");
  const auto sequenceBefore = rawQuery("SELECT seq FROM sqlite_sequence WHERE name = 'item_field';");
  {
    SqliteRepository repository;
    if (!success(repository.open(dbPath.string()), "Open Qt v20 database")) return 1;
    lexicon::LexiconApplication app(repository);
    if (!expect(rawQuery("SELECT sql FROM sqlite_master WHERE name = 'item_field';").find("BETWEEN 0 AND 10") !=
                    std::string::npos, "item_field does not admit Image") ||
        !expect(!fieldsBefore.empty() && rawQuery("SELECT group_concat(id || ':' || name, ',') FROM "
                                                  "(SELECT * FROM item_field ORDER BY id);") == fieldsBefore,
                "The rebuilt item_field changed its fields") ||
        !expect(valuesBefore != "0" && rawQuery("SELECT COUNT(*) FROM item_value;") == valuesBefore,
                "Values were lost when item_field was rebuilt") ||
        !expect(rawQuery("SELECT seq FROM sqlite_sequence WHERE name = 'item_field';") == sequenceBefore,
                "The field ID sequence moved") ||
        !expect(rawQuery("SELECT COUNT(*) FROM sqlite_master WHERE name IN ('item_value_scope_insert', "
                         "'item_value_scope_update', 'item_field_type_name_unique', "
                         "'idx_item_field_type_position');") == "4",
                "The rebuild lost a trigger or an index") ||
        !expect(rawQuery("SELECT COUNT(*) FROM temp.sqlite_master;") == "0" &&
                    rawQuery("SELECT COUNT(*) FROM sqlite_master WHERE name LIKE 'migration_%';") == "0",
                "The rebuild left a working table")) return 1;
    if (!expect(rawQuery("SELECT version FROM db_version;") == "28" &&
                    rawQuery("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'card';") == "1",
                "The Qt v20 database did not reach version 28 with its card table")) return 1;
    auto found = app.search.findItemId("Příliš žluťoučký kůň", "česky");
    if (!success(found, "Find Qt UTF-8 item")) return 1;
    auto item = app.items.loadItem(*found);
    if (!success(item, "Load Qt item") ||
        !expect(item->fieldValues.begin()->second == "Význam" && item->content == "Žádný problém.",
                "Qt v20 data changed")) return 1;
    auto fields = app.types.loadItemFields(1);
    if (!success(fields, "Load Qt enum options") ||
        !expect(fields->size() == 2 && fields->at(1).enumOptions ==
                  std::vector<std::string>({"A", "École", "quote \""}) &&
                  item->fieldValues.at(2) == "École", "Qt JSON enum options changed")) return 1;
    auto links = app.links.loadLinks(1);
    if (!success(links, "Load Qt links") ||
        !expect(links->size() == 1 && links->front().toItemId == 2, "Qt link changed")) return 1;
    auto settings = app.configuration.loadConfiguration();
    if (!success(settings, "Load Qt configuration") ||
        !expect(settings->at("view.theme") == "tmavý", "Qt configuration changed")) return 1;
    auto groupId = app.groups.defaultGroupId();
    if (!success(groupId, "Default group")) return 1;
    auto existingGroups = app.groups.loadGroups();
    if (!success(existingGroups, "Load Qt v20 groups") ||
        !expect(std::any_of(existingGroups->begin(), existingGroups->end(),
                            [&](const auto &group) {
                              return group.id == *groupId && group.name == "Default";
                            }), "Qt v20 Default Group changed")) return 1;
    lexicon::ItemRecord newItem;
    newItem.groupId = *groupId;
    newItem.title = "Persistent item";
    newItem.aliases = {"NOTE", "note", "École", "école"};
    auto created = app.items.createItem(newItem);
    if (!success(created, "Create item in Qt database")) return 1;
    lexicon::ItemRecord related;
    related.groupId = *groupId;
    related.title = "Linked creation";
    lexicon::LinkRecord relation;
    relation.toItemId = *created;
    relation.linkType = lexicon::LinkType::Related;
    auto linkedId = app.items.createItem(related, {relation});
    if (!success(linkedId, "Create item with link")) return 1;
    auto linked = app.links.loadLinks(*linkedId);
    if (!success(linked, "Load created link") ||
        !expect(linked->size() == 1 && linked->front().toItemId == *created,
                "Create item did not persist link")) return 1;
    auto backlinks = app.links.loadBacklinks(*created);
    if (!success(backlinks, "Load created backlink") ||
        !expect(backlinks->size() == 1 && backlinks->front().fromItemId == *linkedId,
                "Backlink was not persisted")) return 1;
    if (!success(app.links.deleteLink(linked->front().id), "Delete link")) return 1;
    auto deletedLinks = app.links.loadLinks(*linkedId);
    if (!success(deletedLinks, "Reload deleted links") ||
        !expect(deletedLinks->empty(), "Link deletion failed")) return 1;
    auto loaded = app.items.loadItem(*created);
    if (!success(loaded, "Load created item") ||
        !expect(loaded->aliases.size() == 3, "ASCII aliases were not deduplicated or UTF-8 aliases were collapsed")) return 1;
    auto suggestions = app.search.loadSuggestions();
    if (!success(suggestions, "Load suggestions")) return 1;
    bool upper = false, lower = false;
    for (const auto &value : *suggestions) { upper |= value == "École"; lower |= value == "école"; }
    if (!expect(upper && lower, "Non-ASCII case was folded unexpectedly")) return 1;
    auto asciiMatches = app.items.countItems(-1, -1, {}, "PERSISTENT", {}, {}, "", "", -1, -1, -1);
    if (!success(asciiMatches, "ASCII search") || !expect(*asciiMatches == 1, "ASCII search changed")) return 1;
    auto nonAsciiExact = app.items.countItems(-1, -1, {}, "école", {}, {}, "", "", -1, -1, -1);
    if (!success(nonAsciiExact, "UTF-8 search") || !expect(*nonAsciiExact == 1, "Exact UTF-8 search failed")) return 1;
    lexicon::ItemRecord onlyUpper;
    onlyUpper.groupId = *groupId;
    onlyUpper.title = "Étiquette";
    auto upperId = app.items.createItem(onlyUpper);
    if (!success(upperId, "Create uppercase UTF-8 item")) return 1;
    auto exactMatch = app.items.countItems(-1, -1, {}, "Étiquette", {}, {});
    auto foldedMatch = app.items.countItems(-1, -1, {}, "étiquette", {}, {});
    // The substring search folds ASCII case only; the full-text index, where
    // SQLite has FTS5, folds case and diacritics in every script.
    if (!success(exactMatch, "Exact UTF-8 search") ||
        !success(foldedMatch, "Non-ASCII case search") ||
        !expect(*exactMatch == 1 && *foldedMatch == (fts5Available() ? 1 : 0),
                "Non-ASCII case folding does not follow the search index")) return 1;
    loaded->title = "Updated item";
    if (!success(app.items.saveItem(*loaded), "Update item")) return 1;
    auto reloaded = app.items.loadItem(*created);
    if (!success(reloaded, "Reload updated item") || !expect(reloaded->title == "Updated item", "Update failed")) return 1;
    lexicon::ItemFieldRecord newField;
    newField.itemTypeId = 1;
    newField.name = "New choice";
    newField.dataType = lexicon::FieldDataType::Enum;
    newField.enumOptions = {"A", "a", "É", "é", "quoted \""};
    if (!success(app.types.upsertItemField(newField), "Save enum options")) return 1;
    auto roundTripFields = app.types.loadItemFields(1);
    if (!success(roundTripFields, "Reload enum options") ||
        !expect(std::find_if(roundTripFields->begin(), roundTripFields->end(),
                              [](const auto &field) { return field.name == "New choice"; })->enumOptions ==
                  std::vector<std::string>({"A", "quoted \"", "É", "é"}),
                "Enum JSON round trip or case policy failed")) return 1;
    lexicon::ItemFieldRecord imageField;
    imageField.itemTypeId = 1;
    imageField.name = "Picture";
    imageField.dataType = lexicon::FieldDataType::Image;
    if (!success(app.types.upsertItemField(imageField), "Save an Image field")) return 1;
    // Foreign keys are enforced again: deleting a field takes its values.
    const auto valuesOfFirstField = rawQuery("SELECT COUNT(*) FROM item_value WHERE item_field_id = 2;");
    if (!expect(valuesOfFirstField != "0", "The fixture has values for field 2") ||
        !success(app.types.deleteItemField(2), "Delete a migrated field") ||
        !expect(rawQuery("SELECT COUNT(*) FROM item_value WHERE item_field_id = 2;") == "0",
                "Deleting a rebuilt field left its values")) return 1;
    const auto source = temp.path / "blob-source.bin";
    const auto exported = temp.path / "blob-export.bin";
    { std::ofstream stream(source, std::ios::binary); stream << "abc"; }
    auto hash = app.blobs.importFile(source.string());
    if (!success(hash, "Import blob") ||
        !expect(*hash == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                "Blob SHA-256 changed")) return 1;
    { std::ofstream stream(exported, std::ios::binary); stream << "old"; }
    if (!success(app.blobs.exportFile(*hash, exported.string()), "Export blob")) return 1;
    std::ifstream blob(exported, std::ios::binary);
    if (!expect(std::string(std::istreambuf_iterator<char>(blob), {}) == "abc", "Blob bytes changed")) return 1;
    if (!success(app.items.deleteItem(*created), "Delete item")) return 1;
    auto deleted = app.items.loadItem(*created);
    if (!expect(!deleted && deleted.error().code == lexicon::Error::Code::NotFound, "Delete failed")) return 1;

    lexicon::ItemRecord rollbackItem;
    rollbackItem.groupId = *groupId;
    rollbackItem.title = "Must roll back";
    lexicon::LinkRecord badLink;
    badLink.toItemId = 999999;
    badLink.linkType = lexicon::LinkType::Related;
    auto failed = app.items.createItem(rollbackItem, {badLink});
    if (!expect(!failed && failed.error().code == lexicon::Error::Code::Storage, "Expected foreign-key failure")) return 1;
    auto rolledBack = app.search.findItemId("Must roll back", "");
    if (!expect(!rolledBack && rolledBack.error().code == lexicon::Error::Code::NotFound,
                "Item survived failed link insert")) return 1;
  }
  // Migration 26 on a database at version 25, the last one without cards:
  // the table is added and what was there stays as it was.
  {
    const auto v25 = temp.path / "v25.db";
    int kept = -1;
    {
      SqliteRepository repository;
      if (!success(repository.open(v25.string()), "Create a database")) return 1;
      lexicon::LexiconApplication app(repository);
      lexicon::ItemRecord item;
      item.groupId = app.groups.defaultGroupId().value_or(-1);
      item.title = "Před kartami";
      item.content = "Written at version 25.";
      auto created = app.items.createItem(item);
      if (!success(created, "Create an item at version 25")) return 1;
      kept = *created;
    }
    const auto run = [&v25](const std::string &sql) {
      sqlite3 *raw = nullptr;
      const bool done = sqlite3_open(v25.string().c_str(), &raw) == SQLITE_OK &&
                        sqlite3_exec(raw, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
      sqlite3_close(raw);
      return done;
    };
    const auto query = [&v25](const std::string &sql) {
      sqlite3 *raw = nullptr;
      std::string result;
      if (sqlite3_open(v25.string().c_str(), &raw) == SQLITE_OK) {
        sqlite3_stmt *statement = nullptr;
        if (sqlite3_prepare_v2(raw, sql.c_str(), -1, &statement, nullptr) == SQLITE_OK &&
            sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_text(statement, 0))
          result = reinterpret_cast<const char *>(sqlite3_column_text(statement, 0));
        sqlite3_finalize(statement);
      }
      sqlite3_close(raw);
      return result;
    };
    // Remove the structures introduced by migrations 26–28 to reconstruct
    // the last database version without cards, history or recurring alarms.
    if (!expect(run("DROP INDEX idx_alarm_item_id; "
                    "ALTER TABLE alarm DROP COLUMN anchor_at; "
                    "ALTER TABLE alarm DROP COLUMN item_id; "
                    "ALTER TABLE alarm DROP COLUMN repeat_days; "
                    "DROP TABLE item_history; DROP TABLE card; "
                    "UPDATE db_version SET version = 25;") &&
                    query("SELECT COUNT(*) FROM sqlite_master WHERE name IN ('card', 'idx_card_item_id');") == "0",
                "Could not take the database back to version 25")) return 1;
    SqliteRepository repository;
    if (!success(repository.open(v25.string()), "Migrate a version 25 database")) return 1;
    lexicon::LexiconApplication app(repository);
    if (!expect(query("SELECT version FROM db_version;") == "28", "Migrations 26–28 did not run") ||
        !expect(query("SELECT COUNT(*) FROM sqlite_master WHERE name IN ('card', 'idx_card_item_id');") == "2",
                "Migration 26 did not add the card table and its index")) return 1;
    auto item = app.items.loadItem(kept);
    if (!success(item, "Load the item written at version 25") ||
        !expect(item->title == "Před kartami" && item->content == "Written at version 25.",
                "Migration 26 changed an item")) return 1;
    auto card = app.cards.createCard(kept, "Co bylo dřív?", "Verze 25.");
    if (!success(card, "Add a card after the upgrade") ||
        !success(app.cards.recordAttempt(card->id, false), "Answer it")) return 1;
    if (!success(app.items.deleteItem(kept), "Delete the upgraded item") ||
        !expect(query("SELECT COUNT(*) FROM card;") == "0", "The upgraded card table does not cascade")) return 1;
  }
  // An older binary must not open and write a schema it does not understand.
  {
    const auto future = temp.path / "future.db";
    {
      SqliteRepository repository;
      if (!success(repository.open(future.string()), "Create future-version fixture")) return 1;
    }
    sqlite3 *raw = nullptr;
    const bool bumped = sqlite3_open(future.string().c_str(), &raw) == SQLITE_OK &&
                        sqlite3_exec(raw, "UPDATE db_version SET version = 29;", nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(raw);
    if (!expect(bumped, "Could not mark fixture as a newer schema")) return 1;
    SqliteRepository repository;
    auto opened = repository.open(future.string());
    if (!expect(!opened && opened.error().code == lexicon::Error::Code::Storage &&
                    opened.error().message.find("newer") != std::string::npos,
                "A newer database schema was accepted")) return 1;
  }
  if (!expect(lexicon::asciiFold("ABCéÉ") == "abcéÉ", "Core case policy changed")) return 1;
  return 0;
}
