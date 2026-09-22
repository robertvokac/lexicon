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
  {
    SqliteRepository repository;
    if (!success(repository.open(dbPath.string()), "Open Qt v20 database")) return 1;
    lexicon::LexiconApplication app(repository);
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
  if (!expect(lexicon::asciiFold("ABCéÉ") == "abcéÉ", "Core case policy changed")) return 1;
  return 0;
}
