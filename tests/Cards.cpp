// Cards: questions and answers about an item, what a quiz answer counts, and
// the quiz over an item or its neighbourhood - which is not Review.
#include "LexiconApplication.h"
#include "SqliteRepository.h"

#include <sqlite3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace fs = std::filesystem;
using lexicon::CardRecord;
using lexicon::Error;
int failures = 0;
void check(bool condition, const std::string &message) {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}
struct TemporaryDirectory {
  fs::path path = fs::temp_directory_path() /
      ("lexicon-cards-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  TemporaryDirectory() { fs::create_directories(path); }
  ~TemporaryDirectory() { std::error_code error; fs::remove_all(path, error); }
};
// The first column of the first row, read behind the repository's back.
std::string scalar(const std::string &path, const std::string &sql) {
  sqlite3 *db = nullptr;
  std::string result;
  if (sqlite3_open(path.c_str(), &db) == SQLITE_OK) {
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_text(statement, 0))
      result = reinterpret_cast<const char *>(sqlite3_column_text(statement, 0));
    sqlite3_finalize(statement);
  }
  sqlite3_close(db);
  return result;
}
bool execute(const std::string &path, const std::string &sql) {
  sqlite3 *db = nullptr;
  bool done = sqlite3_open(path.c_str(), &db) == SQLITE_OK &&
              sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr) == SQLITE_OK &&
              sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
  sqlite3_close(db);
  return done;
}
bool utc(const std::string &text) {
  static const std::regex pattern(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$)");
  return std::regex_match(text, pattern);
}
template <class T> T value(lexicon::Result<T> result, const std::string &what) {
  check(result.has_value(), what + (result ? "" : ": " + result.error().message));
  return result ? std::move(*result) : T{};
}
template <class T> bool refused(const lexicon::Result<T> &result, Error::Code code) {
  return !result && result.error().code == code;
}
std::vector<int> ids(const std::vector<lexicon::QuizCard> &cards) {
  std::vector<int> result;
  for (const auto &entry : cards) result.push_back(entry.card.id);
  return result;
}
} // namespace

int main() {
  TemporaryDirectory directory;
  const auto path = (directory.path / "lexicon.db").string();
  SqliteRepository repository;
  if (!repository.open(path)) return 1;
  lexicon::LexiconApplication app(repository);
  const int group = value(app.groups.defaultGroupId(), "the Default group");
  const auto create = [&](const std::string &title) {
    lexicon::ItemRecord item;
    item.groupId = group;
    item.title = title;
    return value(app.items.createItem(item), "create " + title);
  };

  // Migration 26 made the table, with an index to load one item's cards.
  check(scalar(path, "SELECT version FROM db_version;") == "28", "a fresh database is at version 28");
  check(scalar(path, "SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name = 'idx_card_item_id' "
                     "AND sql LIKE '%card(item_id, id)%';") == "1",
        "cards are indexed by item");
  const auto columns = scalar(path, "SELECT group_concat(name, ',') FROM "
                                    "(SELECT name FROM pragma_table_info('card') ORDER BY cid);");
  check(columns == "id,item_id,question,answer,success_count,failure_count,last_attempt",
        "the card table has its columns, got " + columns);

  // Create, load, list.
  const int provenance = create("pointer provenance");
  const auto before = value(app.items.loadItem(provenance), "load the item");
  const auto first = value(app.cards.createCard(provenance, "What does pointer provenance describe?",
                                                "Where a pointer came from:\nthe object it may reach."),
                           "create a card");
  check(first.id > 0 && first.itemId == provenance, "a card belongs to its item");
  check(first.successCount == 0 && first.failureCount == 0 && first.lastAttempt.empty(),
        "a new card has never been answered");
  check(first.answer == "Where a pointer came from:\nthe object it may reach.", "the answer keeps its lines");
  const auto second = value(app.cards.createCard(provenance, "Why can provenance matter to compiler optimizations?",
                                                 "Alias analysis relies on it."),
                            "create a second card");
  const std::string question = "Co znamená řetězec?\nstd::uint64_t";
  const std::string answer = "Příliš žluťoučký kůň\n指针";
  const auto unicode = value(app.cards.createCard(provenance, question, answer), "create a UTF-8 card");
  const auto loaded = value(app.cards.loadCard(unicode.id), "load the UTF-8 card");
  check(loaded.question == question && loaded.answer == answer, "UTF-8 and new lines survive byte for byte");
  auto cards = value(app.cards.loadCards(provenance), "the item's cards");
  check(cards.size() == 3 && cards[0].id == first.id && cards[1].id == second.id && cards[2].id == unicode.id,
        "an item's cards come in the order they were added");

  // Edit: the text changes, nothing else.
  auto edited = value(app.cards.updateCard(first.id, "What does provenance describe?", "The origin of a pointer."),
                      "edit a card");
  check(edited.question == "What does provenance describe?" && edited.answer == "The origin of a pointer." &&
            edited.itemId == provenance && edited.successCount == 0 && edited.lastAttempt.empty(),
        "an edit changes the question and the answer only");

  // Delete.
  check(app.cards.deleteCard(second.id).has_value(), "delete a card");
  check(refused(app.cards.loadCard(second.id), Error::Code::NotFound), "a deleted card is gone");
  check(refused(app.cards.deleteCard(second.id), Error::Code::NotFound), "deleting it twice is a not-found error");
  check(value(app.cards.loadCards(provenance), "cards after the delete").size() == 2, "two cards are left");

  // Missing.
  check(refused(app.cards.loadCard(999999), Error::Code::NotFound), "a missing card is not found");
  check(refused(app.cards.updateCard(999999, "Q", "A"), Error::Code::NotFound), "a missing card cannot be edited");
  check(refused(app.cards.recordAttempt(999999, true), Error::Code::NotFound), "a missing card cannot be answered");
  check(refused(app.cards.loadCards(999999), Error::Code::NotFound), "a missing item has no card list");

  // Validation.
  const auto count = [&] { return scalar(path, "SELECT COUNT(*) FROM card;"); };
  const auto stored = count();
  check(refused(app.cards.createCard(999999, "Q", "A"), Error::Code::NotFound), "a card needs an existing item");
  check(refused(app.cards.createCard(-1, "Q", "A"), Error::Code::Validation), "a card needs an item");
  check(refused(app.cards.createCard(provenance, "  \n\t", "A"), Error::Code::Validation), "a card needs a question");
  check(refused(app.cards.createCard(provenance, "Q", "\r\n "), Error::Code::Validation), "a card needs an answer");
  check(refused(app.cards.updateCard(first.id, "", "A"), Error::Code::Validation), "an edit keeps a question");
  check(refused(app.cards.updateCard(first.id, "Q", " "), Error::Code::Validation), "an edit keeps an answer");
  check(value(app.cards.loadCard(first.id), "the card after a refused edit").question == edited.question,
        "a refused edit changes nothing");
  // What an import brings is checked the same way.
  check(refused(repository.createCard({-1, provenance, "Q", "A", -1, 0, ""}), Error::Code::Validation),
        "a negative success count is refused");
  check(refused(repository.createCard({-1, provenance, "Q", "A", 0, -3, ""}), Error::Code::Validation),
        "a negative failure count is refused");
  check(refused(repository.createCard({-1, provenance, "Q", "A", 1, 0, "yesterday"}), Error::Code::Validation),
        "a last attempt that is not a UTC time is refused");
  check(refused(repository.createCard({-1, provenance, "Q", "A", 10, 5, ""}), Error::Code::Validation),
        "answers without a last attempt are refused");
  check(refused(repository.createCard({-1, provenance, "Q", "A", 0, 0, "2026-09-24T14:00:00Z"}),
                Error::Code::Validation),
        "a last attempt of a card never answered is refused");
  check(count() == stored, "nothing refused was stored");
  const int imported = value(repository.createCard({-1, provenance, "Imported?", "Yes.", 7, 3, "2026-09-24T14:00:00Z"}),
                             "store a card with its statistics, as an import does");
  const auto kept = value(app.cards.loadCard(imported), "load the imported card");
  check(kept.successCount == 7 && kept.failureCount == 3 && kept.lastAttempt == "2026-09-24T14:00:00Z",
        "an imported card keeps its statistics");
  // The database itself refuses what no Lexicon should write.
  check(!execute(path, "UPDATE card SET success_count = -1 WHERE id = " + std::to_string(imported) + ";"),
        "the database refuses a negative count");
  check(!execute(path, "INSERT INTO card(item_id, question, answer) VALUES(" + std::to_string(provenance) +
                           ", ' ', 'A');"),
        "the database refuses a blank question");
  check(!execute(path, "INSERT INTO card(item_id, question, answer) VALUES(999999, 'Q', 'A');"),
        "the database refuses a card of no item");
  check(refused(app.cards.quizCards(provenance, -1, 150), Error::Code::Validation) &&
            refused(app.cards.quizCards(provenance, 4, 150), Error::Code::Validation) &&
            refused(app.cards.quizCards(provenance, 1, 0), Error::Code::Validation),
        "a quiz reaches 0 to 3 links and at least one item");
  check(refused(app.cards.quizCards(999999, 0, 150), Error::Code::NotFound), "a missing item has no quiz");

  // Attempts: Yes counts a success, No a failure, both stamp the time.
  const auto dueBefore = value(app.review.countDue(-1), "items due before");
  const std::string start = scalar(path, "SELECT strftime('%Y-%m-%dT%H:%M:%SZ', 'now');");
  const auto yes = value(app.cards.recordAttempt(first.id, true), "answer Yes");
  check(yes.successCount == 1 && yes.failureCount == 0, "Yes counts a success");
  check(utc(yes.lastAttempt), "the attempt time is UTC, got " + yes.lastAttempt);
  const auto no = value(app.cards.recordAttempt(first.id, false), "answer No");
  check(no.successCount == 1 && no.failureCount == 1, "No counts a failure");
  check(utc(no.lastAttempt) && no.lastAttempt >= yes.lastAttempt, "No stamps the time too");
  for (int i = 0; i < 3; ++i) value(app.cards.recordAttempt(first.id, true), "answer Yes again");
  const auto answered = value(app.cards.loadCard(first.id), "the answered card");
  const std::string end = scalar(path, "SELECT strftime('%Y-%m-%dT%H:%M:%SZ', 'now');");
  check(answered.successCount == 4 && answered.failureCount == 1, "repeated answers accumulate");
  check(answered.lastAttempt >= start && answered.lastAttempt <= end,
        "the time is the database's clock, got " + answered.lastAttempt);
  // An edit never resets what was counted.
  const auto reworded = value(app.cards.updateCard(first.id, "What is provenance?", "A pointer's origin."), "edit again");
  check(reworded.successCount == 4 && reworded.failureCount == 1 && reworded.lastAttempt == answered.lastAttempt,
        "editing a card keeps its statistics");

  // Cards are not Review: none of this moved the item's review or revision.
  const auto after = value(app.items.loadItem(provenance), "load the item again");
  check(after.understanding == before.understanding, "a card answer leaves the understanding alone");
  check(after.reviewedAt == before.reviewedAt && after.reviewedAt.empty(), "and the review time");
  check(after.reviewDueAt == before.reviewDueAt, "and the next review");
  check(after.revision == before.revision, "and the item's revision, so no editor of it sees a conflict");
  check(value(app.review.countDue(-1), "items due after") == dueBefore, "the review queue is as it was");
  // Nor does a review move a card.
  check(app.review.review(provenance, lexicon::ReviewRating::Good).has_value(), "review the item");
  const auto untouched = value(app.cards.loadCard(first.id), "the card after a review");
  check(untouched.successCount == 4 && untouched.failureCount == 1 && untouched.lastAttempt == answered.lastAttempt,
        "a review leaves the cards alone");

  // Answers given at once on several connections are all counted: the
  // increment is one statement, never read, add and write back.
  {
    const auto shared = value(app.cards.createCard(provenance, "Concurrent?", "Every answer counts."), "a shared card");
    constexpr int kThreads = 4;
    constexpr int kAttempts = 25;
    std::atomic<int> errors{0};
    std::vector<std::thread> threads;
    for (int thread = 0; thread < kThreads; ++thread) {
      threads.emplace_back([&, thread] {
        SqliteRepository connection;
        if (!connection.open(path)) {
          ++errors;
          return;
        }
        for (int attempt = 0; attempt < kAttempts; ++attempt)
          if (!connection.recordCardAttempt(shared.id, (thread + attempt) % 2 == 0)) ++errors;
      });
    }
    for (auto &thread : threads) thread.join();
    const auto counted = value(app.cards.loadCard(shared.id), "the shared card");
    check(errors == 0, "every concurrent answer was accepted");
    check(counted.successCount == kThreads * kAttempts / 2 && counted.failureCount == kThreads * kAttempts / 2,
          "no concurrent answer is lost: " + std::to_string(counted.successCount) + " Yes, " +
              std::to_string(counted.failureCount) + " No");
  }

  // Deleting an item deletes its cards through the foreign key.
  {
    const int doomed = create("Doomed");
    const auto card = value(app.cards.createCard(doomed, "Q1", "A1"), "a card of the doomed item");
    value(app.cards.createCard(doomed, "Q2", "A2"), "another");
    check(app.items.deleteItem(doomed).has_value(), "delete the item");
    check(scalar(path, "SELECT COUNT(*) FROM card WHERE item_id = " + std::to_string(doomed) + ";") == "0",
          "its cards went with it");
    check(refused(app.cards.loadCard(card.id), Error::Code::NotFound), "a card of a deleted item is not found");
    check(scalar(path, "SELECT COUNT(*) FROM pragma_foreign_key_check;") == "0", "no card is left without its item");
  }

  // The quiz: the item alone, or its neighbourhood as the graph finds it.
  // centre -> near (Uses), centre -> empty (Related), other -> centre
  // (Related), other -> near (a cycle), near -> far (IsA), far -> farthest
  // (PartOf). Alone is linked to nothing.
  const int centre = create("Centre"), near = create("Near"), empty = create("No cards");
  const int other = create("Other"), far = create("Far"), farthest = create("Farthest"), alone = create("Alone");
  const auto link = [&](int from, int to, lexicon::LinkType type) {
    check(app.links.saveLink({-1, from, to, type, 0, "", "", ""}).has_value(), "link");
  };
  link(centre, near, lexicon::LinkType::Uses);
  link(centre, empty, lexicon::LinkType::Related);
  link(other, centre, lexicon::LinkType::Related);
  link(other, near, lexicon::LinkType::Related);
  link(near, far, lexicon::LinkType::IsA);
  link(far, farthest, lexicon::LinkType::PartOf);
  for (const auto &[item, name] : std::vector<std::pair<int, std::string>>{
           {centre, "centre 1"}, {centre, "centre 2"}, {near, "near"}, {other, "other"},
           {far, "far"}, {farthest, "farthest"}, {alone, "alone"}})
    value(app.cards.createCard(item, name + "?", name + "."), "a card for " + name);
  // What the graph shows, with each item's cards in their order.
  const auto expected = [&](int depth, int maxNodes) {
    std::vector<int> result;
    for (const auto &node : value(app.links.neighborhood(centre, depth, maxNodes), "the neighbourhood").nodes)
      for (const auto &card : value(app.cards.loadCards(node.item.id), "cards of " + node.item.title))
        result.push_back(card.id);
    return result;
  };
  const auto alonePlain = value(app.cards.quizCards(centre, 0, 150), "quiz the centre alone");
  check(alonePlain.cards.size() == 2 && alonePlain.itemCount == 1 && !alonePlain.truncated,
        "depth 0 quizzes the item's own cards");
  check(alonePlain.cards[0].itemId == centre && alonePlain.cards[0].itemTitle == "Centre" &&
            alonePlain.cards[0].card.question == "centre 1?" && alonePlain.cards[1].card.question == "centre 2?",
        "each card says which item it asks about, in the order added");
  const auto one = value(app.cards.quizCards(centre, 1, 150), "quiz one link away");
  check(one.itemCount == 4 && !one.truncated, "depth 1 covers the centre and its three neighbours, cards or not");
  check(ids(one.cards) == expected(1, 150) && one.cards.size() == 4, "depth 1 adds Near and Other, in graph order");
  std::set<std::string> titles;
  for (const auto &entry : one.cards) titles.insert(entry.itemTitle);
  check(titles == std::set<std::string>{"Centre", "Near", "Other"}, "cards come from more than one item");
  const auto two = value(app.cards.quizCards(centre, 2, 150), "quiz two links away");
  check(ids(two.cards) == expected(2, 150) && two.cards.size() == 5 && two.itemCount == 5, "depth 2 adds Far");
  const auto three = value(app.cards.quizCards(centre, 3, 150), "quiz three links away");
  check(ids(three.cards) == expected(3, 150) && three.cards.size() == 6 && three.itemCount == 6,
        "depth 3 adds Farthest, and Alone is never reached");
  const auto distinct = ids(three.cards);
  check(std::set<int>(distinct.begin(), distinct.end()).size() == distinct.size(),
        "the cycle through Other and Near brings no card twice");
  const auto capped = value(app.cards.quizCards(centre, 3, 2), "quiz with room for two items");
  check(capped.truncated && capped.itemCount == 2 && ids(capped.cards) == expected(3, 2),
        "a quiz larger than allowed says it was cut");
  const auto none = value(app.cards.quizCards(empty, 0, 150), "quiz an item without cards");
  check(none.cards.empty() && none.itemCount == 1, "an item without cards gives an empty quiz");
  const auto rows = value(repository.loadCardsForItems({near, centre, near, 999999}), "cards of several items");
  check(rows.size() == 3 && rows[0].itemId == near && rows[1].itemId == centre && rows[2].itemId == centre,
        "cards of several items come item by item, each item once");

  if (failures == 0) std::cout << "cards: all checks passed\n";
  return failures == 0 ? 0 : 1;
}
