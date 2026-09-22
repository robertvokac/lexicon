// Item search: content through the full-text index, diacritics, the order of
// the results, and an index that follows changes made by anyone.
#include "LexiconApplication.h"
#include "SqliteRepository.h"

#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
int failures = 0;
void check(bool condition, const std::string &message) {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}
struct TemporaryDirectory {
  fs::path path = fs::temp_directory_path() /
      ("lexicon-search-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  TemporaryDirectory() { fs::create_directories(path); }
  ~TemporaryDirectory() { std::error_code error; fs::remove_all(path, error); }
};

bool fts5(const std::string &path) {
  sqlite3 *db = nullptr;
  if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) return false;
  const bool available =
      sqlite3_exec(db, "CREATE VIRTUAL TABLE temp.probe USING fts5(x);", nullptr, nullptr, nullptr) == SQLITE_OK;
  sqlite3_close(db);
  return available;
}

// Another program writing to the database: no repository, no index upkeep.
void writeBehindTheRepository(const std::string &path, const std::string &sql) {
  sqlite3 *db = nullptr;
  check(sqlite3_open(path.c_str(), &db) == SQLITE_OK, "open the database directly");
  check(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK, "write directly: " + sql);
  sqlite3_close(db);
}

class Search {
public:
  explicit Search(lexicon::LexiconApplication &app) : app_(app) {}
  std::vector<std::string> titles(const std::string &text, int sortColumn = 0,
                                  lexicon::SortOrder order = lexicon::SortOrder::Ascending) {
    auto items = app_.items.loadItems(-1, -1, {}, text, {}, {}, {}, {}, -1, -1, -1, -1, 0,
                                      sortColumn, order);
    auto count = app_.items.countItems(-1, -1, {}, text, {}, {});
    std::vector<std::string> result;
    if (!items || !count) {
      check(false, "search for '" + text + "' failed");
      return result;
    }
    check(*count == static_cast<int>(items->size()), "count and page agree for '" + text + "'");
    for (const auto &item : *items) result.push_back(item.title);
    return result;
  }

private:
  lexicon::LexiconApplication &app_;
};

std::string joined(const std::vector<std::string> &values) {
  std::string text;
  for (const auto &value : values) text += (text.empty() ? "" : ", ") + value;
  return "[" + text + "]";
}
} // namespace

int main() {
  TemporaryDirectory directory;
  const auto path = (directory.path / "lexicon.db").string();
  const bool fullText = fts5(path);
  std::cout << "search: FTS5 " << (fullText ? "available" : "not available, substring fallback") << '\n';

  SqliteRepository repository;
  if (!repository.open(path)) {
    std::cerr << "cannot open the database\n";
    return 1;
  }
  lexicon::LexiconApplication app(repository);
  const int group = *app.groups.defaultGroupId();
  const auto create = [&](const std::string &title, const std::string &content,
                          std::vector<std::string> aliases = {}) {
    lexicon::ItemRecord item;
    item.groupId = group;
    item.title = title;
    item.content = content;
    item.aliases = std::move(aliases);
    auto id = app.items.createItem(item);
    check(id.has_value(), "create " + title);
    return id.value_or(-1);
  };
  const int semigroup = create("Semigroup", "A monoid without the identity element.");
  create("Monoid homomorphism", "Preserves the operation.");
  const int monoid = create("Monoid", "An algebraic structure.");
  create("Unital magma", "Another name.", {"monoid"});
  const int czech = create("Pangram", "Příliš žluťoučký kůň úpěl ďábelské ódy.");
  create("Language", "C++ is used for the desktop client.");
  Search search(app);

  // The exact title first, then an exact alias, then titles starting with the
  // text, then the items that only mention it in their content. Within each,
  // the chosen column order: here by ID.
  check(search.titles("monoid") ==
            std::vector<std::string>{"Monoid", "Unital magma", "Monoid homomorphism", "Semigroup"},
        "ranked results, got " + joined(search.titles("monoid")));
  check(search.titles("monoid", 3, lexicon::SortOrder::Descending).front() == "Monoid",
        "an exact match leads whatever the column order");
  check(search.titles("identity") == std::vector<std::string>{"Semigroup"},
        "content is searched, got " + joined(search.titles("identity")));
  check(search.titles("c++") == std::vector<std::string>{"Language"},
        "a word the index cannot use is still found in the content, got " + joined(search.titles("c++")));
  if (fullText) {
    check(search.titles("zlutoucky kun") == std::vector<std::string>{"Pangram"},
          "diacritics are ignored, got " + joined(search.titles("zlutoucky kun")));
    check(search.titles("algebr") == std::vector<std::string>{"Monoid"},
          "a word prefix matches, got " + joined(search.titles("algebr")));
    check(search.titles("structure monoid") == std::vector<std::string>{"Monoid"},
          "every word must occur, got " + joined(search.titles("structure monoid")));
    check(search.titles("\"structure") == std::vector<std::string>{"Monoid"},
          "quotes in the text are not query syntax");
  }

  // A save through the repository re-indexes the item.
  auto loaded = app.items.loadItem(monoid);
  loaded->content = "A semigroup with an identity.";
  check(app.items.saveItem(*loaded).has_value(), "save new content");
  check(search.titles("algebraic").empty(), "old content is forgotten");
  check(search.titles("semigroup with") ==
            std::vector<std::string>{"Semigroup", "Monoid"},
        "new content is found, got " + joined(search.titles("semigroup with")));

  // So does a change made by another program.
  writeBehindTheRepository(path, "UPDATE item SET content = 'Written elsewhere: quaternion.', "
                                 "revision = revision + 1 WHERE id = " + std::to_string(czech) + ";");
  check(search.titles("quaternion") == std::vector<std::string>{"Pangram"},
        "a change made elsewhere is found");

  check(app.items.deleteItem(semigroup).has_value(), "delete an item");
  check(search.titles("identity") == std::vector<std::string>{"Monoid"},
        "a deleted item is not found, got " + joined(search.titles("identity")));

  if (failures == 0) std::cout << "search: all checks passed\n";
  return failures == 0 ? 0 : 1;
}
