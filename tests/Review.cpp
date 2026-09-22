// Review: which items are due, in what order, and what a rating does.
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
using lexicon::ReviewRating;
using lexicon::UnderstandingLevel;
int failures = 0;
void check(bool condition, const std::string &message) {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}
struct TemporaryDirectory {
  fs::path path = fs::temp_directory_path() /
      ("lexicon-review-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  TemporaryDirectory() { fs::create_directories(path); }
  ~TemporaryDirectory() { std::error_code error; fs::remove_all(path, error); }
};
void sql(const std::string &path, const std::string &statement) {
  sqlite3 *db = nullptr;
  check(sqlite3_open(path.c_str(), &db) == SQLITE_OK, "open the database directly");
  check(sqlite3_exec(db, statement.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK, statement);
  sqlite3_close(db);
}
} // namespace

int main() {
  static_assert(lexicon::levelAfterReview(UnderstandingLevel::Understood, ReviewRating::Again) ==
                UnderstandingLevel::Recognized);
  static_assert(lexicon::levelAfterReview(UnderstandingLevel::Unknown, ReviewRating::Again) ==
                UnderstandingLevel::Unknown);
  static_assert(lexicon::levelAfterReview(UnderstandingLevel::Understood, ReviewRating::Hard) ==
                UnderstandingLevel::Understood);
  static_assert(lexicon::levelAfterReview(UnderstandingLevel::Understood, ReviewRating::Good) ==
                UnderstandingLevel::Practiced);
  static_assert(lexicon::levelAfterReview(UnderstandingLevel::Practiced, ReviewRating::Easy) ==
                UnderstandingLevel::Mastered);
  static_assert(lexicon::reviewIntervalDays(UnderstandingLevel::Unknown) <
                lexicon::reviewIntervalDays(UnderstandingLevel::Mastered));

  TemporaryDirectory directory;
  const auto path = (directory.path / "lexicon.db").string();
  SqliteRepository repository;
  if (!repository.open(path)) return 1;
  lexicon::LexiconApplication app(repository);
  const int group = *app.groups.defaultGroupId();
  const auto create = [&](const std::string &title, UnderstandingLevel level) {
    lexicon::ItemRecord item;
    item.groupId = group;
    item.title = title;
    item.understanding = level;
    return app.items.createItem(item).value_or(-1);
  };
  const int functor = create("Functor", UnderstandingLevel::Unknown);
  const int monad = create("Monad", UnderstandingLevel::Practiced);
  const auto titles = [&] {
    std::vector<std::string> result;
    for (const auto &item : app.review.queue(-1, 20).value_or(std::vector<lexicon::ItemRecord>{}))
      result.push_back(item.title);
    return result;
  };
  check(titles() == std::vector<std::string>{"Functor", "Monad"}, "items never reviewed are due, oldest first");
  check(app.review.countDue(-1).value_or(0) == 2, "both are counted as due");

  const int before = app.items.loadItem(functor)->revision;
  auto reviewed = app.review.review(functor, ReviewRating::Good);
  check(reviewed.has_value(), "a review is recorded");
  check(reviewed->understanding == UnderstandingLevel::Recognized, "Good climbs one level");
  check(!reviewed->reviewedAt.empty() && reviewed->reviewedAt.size() == 20 && reviewed->reviewedAt.back() == 'Z',
        "the review time is UTC, got " + reviewed->reviewedAt);
  check(reviewed->reviewDueAt > reviewed->reviewedAt, "the next review is later");
  check(reviewed->revision > before, "a review changes the item's revision");
  check(titles() == std::vector<std::string>{"Monad"}, "a reviewed item is not due at once");
  check(app.review.countDue(-1).value_or(0) == 1, "one item is left");

  // Three days on, Functor (Recognized: two days) is due again, and items
  // reviewed before come first.
  sql(path, "UPDATE item SET reviewed_at = strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-3 days') WHERE id = " +
                std::to_string(functor) + ";");
  check(titles() == std::vector<std::string>{"Functor", "Monad"}, "an overdue item comes before new ones");
  // Understanding raised in the editor pushes the next review out.
  auto edited = *app.items.loadItem(functor);
  edited.understanding = UnderstandingLevel::Mastered;
  check(app.items.saveItem(edited).has_value(), "the understanding is raised in the editor");
  check(titles() == std::vector<std::string>{"Monad"}, "a mastered item waits longer");
  check(!app.items.loadItem(functor)->reviewedAt.empty(), "saving an item keeps its review time");

  auto again = app.review.review(monad, ReviewRating::Again);
  check(again && again->understanding == UnderstandingLevel::Understood, "Again drops a level");
  check(!app.review.review(9999, ReviewRating::Good) &&
            app.review.review(9999, ReviewRating::Good).error().code == lexicon::Error::Code::NotFound,
        "reviewing a missing item is a not-found error");

  const int other = [&] {
    app.groups.upsertGroup({-1, "Elsewhere", "", 1});
    for (const auto &g : *app.groups.loadGroups()) if (g.name == "Elsewhere") return g.id;
    return -1;
  }();
  lexicon::ItemRecord elsewhere;
  elsewhere.groupId = other;
  elsewhere.title = "Elsewhere";
  app.items.createItem(elsewhere);
  check(app.review.countDue(other).value_or(0) == 1 && app.review.queue(other, 20)->size() == 1,
        "the queue can be limited to one group");
  check(app.review.queue(-1, 1)->size() == 1, "and to a number of items");

  if (failures == 0) std::cout << "review: all checks passed\n";
  return failures == 0 ? 0 : 1;
}
