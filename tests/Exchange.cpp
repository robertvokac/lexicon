// Export and import: a dictionary written by one database and merged into
// others, with its files, and the damage an import must refuse.
#include "Exchange.h"
#include "SqliteRepository.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

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
      ("lexicon-exchange-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  TemporaryDirectory() { fs::create_directories(path); }
  ~TemporaryDirectory() { std::error_code error; fs::remove_all(path, error); }
};

// One database in its own directory, so each has its own blobs.
struct Database {
  explicit Database(const fs::path &directory) {
    fs::create_directories(directory);
    check(repository.open((directory / "lexicon.db").string()).has_value(), "open " + directory.string());
  }
  SqliteRepository repository;
  lexicon::LexiconApplication app{repository};

  int groupNamed(const std::string &name) {
    // Held here: a range-for over *loadGroups() would walk a destroyed list.
    const auto groups = app.groups.loadGroups();
    for (const auto &group : *groups)
      if (group.name == name) return group.id;
    return -1;
  }
  int itemNamed(const std::string &title) {
    auto found = app.search.findItemId(title);
    return found.value_or(-1);
  }
  int itemCount() { return app.items.countItems(-1, -1, {}, {}, {}, {}).value_or(-1); }
};

template <class T> T value(lexicon::Result<T> result, const std::string &what) {
  check(result.has_value(), what + (result ? "" : ": " + result.error().message));
  return result ? std::move(*result) : T{};
}
} // namespace

int main() {
  TemporaryDirectory directory;
  Database source(directory.path / "source");
  auto &app = source.app;

  check(app.groups.upsertGroup({-1, "Maths", "Numbers and structures", 3}).has_value(), "create Maths");
  const int maths = source.groupNamed("Maths");
  const int defaultGroup = value(app.groups.defaultGroupId(), "Default");
  check(app.types.upsertItemType({-1, maths, {}, "Concept", "An idea"}).has_value(), "create Concept");
  check(app.types.upsertItemType({-1, -1, {}, "Note", ""}).has_value(), "create Note");
  int conceptType = -1, note = -1;
  for (const auto &type : value(app.types.loadItemTypes(-1), "types")) {
    if (type.name == "Concept") conceptType = type.id;
    if (type.name == "Note") note = type.id;
  }
  check(app.types.upsertItemField({-1, conceptType, "Difficulty", lexicon::FieldDataType::Enum, 0, {"easy", "hard"}}).has_value(), "Difficulty");
  check(app.types.upsertItemField({-1, conceptType, "Diagram", lexicon::FieldDataType::Blob, 1, {}}).has_value(), "Diagram");
  check(app.types.upsertItemField({-1, note, "Page", lexicon::FieldDataType::Integer, 0, {}}).has_value(), "Page");
  const auto conceptFields = value(app.types.loadItemFields(conceptType), "Concept fields");
  const auto noteFields = value(app.types.loadItemFields(note), "Note fields");
  const std::string diagram = std::string("PNG\0\x01\x02 binary", 14);
  const auto hash = value(app.blobs.importData(diagram), "import a file");

  lexicon::ItemRecord monoid;
  monoid.groupId = maths;
  monoid.itemTypeId = conceptType;
  monoid.title = "Monoid";
  monoid.disambiguation = "algebra";
  monoid.content = "# Monoid\n\nPříliš žluťoučký kůň.";
  monoid.aliases = {"Semigroup with unit"};
  monoid.tags = {"algebra"};
  monoid.flags = {"todo"};
  monoid.status = lexicon::ItemStatus::Draft;
  monoid.understanding = lexicon::UnderstandingLevel::Practiced;
  monoid.pinned = true;
  monoid.properties = {{"source", "folklore"}};
  monoid.fieldValues = {{conceptFields[0].id, "hard"}, {conceptFields[1].id, hash}};
  const int monoidId = value(app.items.createItem(monoid), "create Monoid");
  lexicon::ItemRecord semigroup;
  semigroup.groupId = maths;
  semigroup.title = "Semigroup";
  const int semigroupId = value(app.items.createItem(semigroup), "create Semigroup");
  lexicon::ItemRecord reading;
  reading.groupId = defaultGroup;
  reading.itemTypeId = note;
  reading.title = "Reading list";
  reading.fieldValues = {{noteFields[0].id, "42"}};
  const int readingId = value(app.items.createItem(reading), "create Reading list");
  check(app.links.saveLink({-1, monoidId, semigroupId, lexicon::LinkType::IsA, 1, "", "", ""}).has_value(), "IsA link");
  check(app.links.saveLink({-1, readingId, monoidId, lexicon::LinkType::Custom, 0, "cites", "", ""}).has_value(), "Custom link");

  // Cards travel with their item and keep their statistics. Two cards may
  // ask the same thing; neither is dropped.
  const auto recall = value(app.cards.createCard(monoidId, "Co je monoid?\nstd::uint64_t",
                                                 "Pologrupa s jednotkou.\n指针"), "a card for Monoid");
  value(app.cards.recordAttempt(recall.id, true), "answer it");
  value(app.cards.recordAttempt(recall.id, true), "answer it again");
  const auto answered = value(app.cards.recordAttempt(recall.id, false), "and once without knowing");
  value(app.cards.createCard(monoidId, "Unit?", "Yes."), "a second card");
  value(app.cards.createCard(monoidId, "Unit?", "Yes."), "the same card again");
  value(app.cards.createCard(semigroupId, "Associative?", "Always."), "a card for Semigroup");

  const auto withFiles = value(lexicon::exchange::exportDocument(app, true), "export with files");
  const auto withoutFiles = value(lexicon::exchange::exportDocument(app, false), "export without files");
  check(withFiles.find("\"format\": \"lexicon-export\"") != std::string::npos, "the document names its format");
  check(withoutFiles.find("\"blobs\"") == std::string::npos, "files stay out unless asked for");
  const auto exported = nlohmann::json::parse(withFiles);
  // Version 2: a reader of version 1 would drop the cards without a word, so
  // it refuses the document instead.
  check(exported.value("version", 0) == 2, "the export is format version 2");
  check(exported.contains("cards") && exported.at("cards").size() == 4, "the export holds every card");
  if (exported.contains("cards") && exported.at("cards").size() == 4) {
    const auto &card = exported.at("cards").at(0);
    check(card.value("itemId", 0) == monoidId && card.value("question", std::string{}) == recall.question &&
              card.value("answer", std::string{}) == recall.answer,
          "a card is exported with its item and its text");
    check(card.value("successCount", 0) == 2 && card.value("failureCount", 0) == 1 &&
              card.value("lastAttempt", std::string{}) == answered.lastAttempt,
          "and with its statistics");
    check(exported.at("cards").at(1).at("lastAttempt").is_null(), "a card never answered has no last attempt");
  }

  // Into an empty database: everything arrives.
  Database copy(directory.path / "copy");
  const auto report = value(lexicon::exchange::importDocument(copy.app, withFiles), "import into an empty database");
  check(report.itemsCreated == 3 && report.itemsSkipped == 0, "three items are created");
  check(report.linksCreated == 2 && report.blobsImported == 1, "both links and the file arrive");
  check(report.groupsCreated == 1 && report.typesCreated == 2 && report.fieldsCreated == 3,
        "Maths, both types and their fields are created, Default is reused");
  check(report.warnings.empty(), "nothing needed a warning");
  const int copied = copy.itemNamed("Monoid");
  const auto restored = value(copy.app.items.loadItem(copied), "load the imported Monoid");
  check(restored.groupName == "Maths" && restored.itemTypeName == "Concept", "group and type are mapped");
  check(restored.content == monoid.content && restored.disambiguation == "algebra" &&
            restored.aliases == monoid.aliases && restored.tags == monoid.tags &&
            restored.flags == monoid.flags && restored.pinned &&
            restored.status == lexicon::ItemStatus::Draft &&
            restored.understanding == lexicon::UnderstandingLevel::Practiced &&
            restored.properties.size() == 1 && restored.properties[0].value == "folklore",
        "every part of the item survives");
  const auto copiedFields = value(copy.app.types.loadItemFields(restored.itemTypeId), "copied fields");
  check(restored.fieldValues.size() == 2 && restored.fieldValues.at(copiedFields[0].id) == "hard" &&
            restored.fieldValues.at(copiedFields[1].id) == hash,
        "values follow their fields");
  check(value(copy.app.blobs.readData(hash), "read the imported file") == diagram, "the file's bytes survive");
  const auto links = value(copy.app.links.loadLinks(copied), "copied links");
  check(links.size() == 1 && links[0].toItemTitle == "Semigroup" && links[0].linkType == lexicon::LinkType::IsA &&
            links[0].position == 1,
        "the outgoing link survives");
  const auto backlinks = value(copy.app.links.loadBacklinks(copied), "copied backlinks");
  check(backlinks.size() == 1 && backlinks[0].customValue == "cites", "the custom backlink survives");
  check(report.cardsCreated == 4, "every card arrives");
  const auto copiedCards = value(copy.app.cards.loadCards(copied), "the imported cards of Monoid");
  check(copiedCards.size() == 3 && copiedCards[0].question == recall.question && copiedCards[0].answer == recall.answer,
        "a card keeps its item and its UTF-8 lines");
  check(copiedCards.size() == 3 && copiedCards[0].successCount == 2 && copiedCards[0].failureCount == 1 &&
            copiedCards[0].lastAttempt == answered.lastAttempt,
        "and its statistics");
  check(copiedCards.size() == 3 && copiedCards[1].question == "Unit?" && copiedCards[2].question == "Unit?" &&
            copiedCards[1].lastAttempt.empty(),
        "two cards asking the same are both kept");
  check(value(copy.app.cards.loadCards(copy.itemNamed("Semigroup")), "the imported cards of Semigroup").size() == 1,
        "each card goes to its own item");

  // Again: nothing is duplicated.
  const auto again = value(lexicon::exchange::importDocument(copy.app, withFiles), "import a second time");
  check(again.itemsCreated == 0 && again.itemsSkipped == 3 && again.linksCreated == 0 &&
            again.groupsCreated == 0 && again.typesCreated == 0 && again.fieldsCreated == 0,
        "a second import changes nothing");
  check(copy.itemCount() == 3, "still three items");
  check(again.cardsCreated == 0 && value(copy.app.cards.loadCards(copied), "cards after a second import").size() == 3,
        "and no card twice");

  // Into a database whose Concept is shaped differently.
  Database other(directory.path / "other");
  check(other.app.groups.upsertGroup({-1, "Maths", "", 0}).has_value(), "other Maths");
  const int otherMaths = other.groupNamed("Maths");
  check(other.app.types.upsertItemType({-1, otherMaths, {}, "concept", ""}).has_value(), "other concept");
  int otherConcept = -1;
  for (const auto &type : value(other.app.types.loadItemTypes(-1), "other types"))
    if (type.name == "concept") otherConcept = type.id;
  check(other.app.types.upsertItemField({-1, otherConcept, "difficulty", lexicon::FieldDataType::Text, 0, {}}).has_value(),
        "a Text difficulty");
  lexicon::ItemRecord already;
  already.groupId = otherMaths;
  already.title = "Semigroup";
  already.content = "Written here.";
  check(other.app.items.createItem(already).has_value(), "a Semigroup of its own");
  const auto merged = value(lexicon::exchange::importDocument(other.app, withoutFiles), "import into another shape");
  check(merged.itemsCreated == 2 && merged.itemsSkipped == 1, "the Semigroup already there is kept");
  check(merged.typesCreated == 1, "Concept is matched ignoring case, Note is created");
  check(std::any_of(merged.warnings.begin(), merged.warnings.end(),
                    [](const auto &warning) { return warning.find("difficulty") != std::string::npos ||
                                                     warning.find("Difficulty") != std::string::npos; }),
        "a field of another kind is reported");
  check(std::any_of(merged.warnings.begin(), merged.warnings.end(),
                    [](const auto &warning) { return warning.find("file") != std::string::npos; }),
        "a file that did not travel is reported");
  check(value(other.app.items.loadItem(other.itemNamed("Semigroup")), "kept Semigroup").content == "Written here.",
        "an item that was already there is left alone");
  const auto mergedLinks = value(other.app.links.loadLinks(other.itemNamed("Monoid")), "merged links");
  check(mergedLinks.size() == 1 && mergedLinks[0].toItemTitle == "Semigroup",
        "an imported item links to the item that was already there");
  // Monoid has another ID here; its cards follow it, and the Semigroup that
  // was already here keeps the cards it had - none.
  check(other.itemNamed("Monoid") != monoidId, "the imported Monoid has another ID here");
  check(merged.cardsCreated == 3 &&
            value(other.app.cards.loadCards(other.itemNamed("Monoid")), "merged cards").size() == 3,
        "the cards follow their item to its new ID");
  check(value(other.app.cards.loadCards(other.itemNamed("Semigroup")), "kept Semigroup's cards").empty(),
        "an item that was already here gets no cards");

  // An export written before cards existed - version 1 - has none and imports
  // as before.
  auto legacy = nlohmann::json::parse(withoutFiles);
  legacy["version"] = 1;
  legacy.erase("cards");
  Database old(directory.path / "legacy");
  const auto fromLegacy = value(lexicon::exchange::importDocument(old.app, legacy.dump()),
                                "import an export without cards");
  check(fromLegacy.itemsCreated == 3 && fromLegacy.cardsCreated == 0 &&
            std::none_of(fromLegacy.warnings.begin(), fromLegacy.warnings.end(),
                         [](const auto &warning) { return warning.find("card") != std::string::npos; }),
        "an export without cards imports its items, no card and no complaint");
  check(value(old.app.cards.loadCards(old.itemNamed("Monoid")), "cards after a legacy import").empty(),
        "and the items have no cards");
  // A development build wrote version 1 with cards; they are read all the same.
  auto early = nlohmann::json::parse(withoutFiles);
  early["version"] = 1;
  Database earlyCopy(directory.path / "early");
  const auto fromEarly = value(lexicon::exchange::importDocument(earlyCopy.app, early.dump()),
                               "import a version 1 export with cards");
  check(fromEarly.cardsCreated == 4, "a version 1 export's cards are not dropped");

  // Damage is refused before anything is written.
  Database empty(directory.path / "empty");
  const auto refused = [&](const std::string &text, const std::string &what) {
    auto result = lexicon::exchange::importDocument(empty.app, text);
    check(!result && result.error().code == lexicon::Error::Code::Validation, what + " is refused");
    check(empty.itemCount() == 0, what + " writes nothing");
  };
  refused("not json", "text that is not JSON");
  refused(R"({"format":"something-else","version":1})", "another format");
  refused(R"({"format":"lexicon-export","version":3,"groups":[],"types":[],"items":[],"links":[]})", "a newer version");
  refused(R"({"format":"lexicon-export","version":0,"groups":[],"types":[],"items":[],"links":[]})", "version 0");
  refused(R"({"format":"lexicon-export","version":1,"groups":[],"types":[],"items":[{"id":1}],"links":[]})",
          "an item without a title");
  refused(R"({"format":"lexicon-export","version":1,"groups":[],"types":[],"items":[],"links":[],
             "blobs":[{"hash":"00","data":"%%%"}]})", "a file that is not base64");
  const auto withCard = [](const std::string &card) {
    return std::string(R"({"format":"lexicon-export","version":1,"groups":[{"id":1,"name":"G"}],"types":[],)") +
           R"("items":[{"id":1,"groupId":1,"title":"T"}],"links":[],"cards":[)" + card + "]}";
  };
  refused(withCard(R"({"itemId":1,"question":"Q","answer":"A","successCount":-1})"), "a negative success count");
  refused(withCard(R"({"itemId":1,"question":"Q","answer":"A","failureCount":-2})"), "a negative failure count");
  refused(withCard(R"({"itemId":1,"question":"Q","answer":"A","successCount":"3"})"), "a count that is not a number");
  refused(withCard(R"({"itemId":1,"question":" ","answer":"A"})"), "a card without a question");
  refused(withCard(R"({"itemId":1,"question":"Q","answer":"A","lastAttempt":"soon"})"),
          "a last attempt that is not a UTC time");
  refused(withCard(R"({"itemId":1,"question":"Q","answer":"A","successCount":10,"failureCount":5})"),
          "answers without a last attempt");
  refused(withCard(R"({"itemId":1,"question":"Q","answer":"A","lastAttempt":"2026-09-24T14:00:00Z"})"),
          "a last attempt of a card never answered");
  refused(R"({"format":"lexicon-export","version":1,"groups":[],"types":[],"items":[],"links":[],"cards":{}})",
          "cards that are not a list");
  // A failure half way rolls everything back.
  auto broken = lexicon::exchange::importDocument(empty.app,
      std::string(R"({"format":"lexicon-export","version":1,"groups":[{"id":1,"name":"G"}],"types":[],)") +
      R"("items":[{"id":1,"groupId":1,"title":"Fine"},{"id":2,"groupId":1,"title":"  "}],"links":[]})");
  check(!broken, "an item with a blank title fails the import");
  check(empty.itemCount() == 0 && empty.groupNamed("G") < 0, "and leaves nothing behind");

  // A portable export must never claim success if a referenced file is gone.
  // Restoring such a document would otherwise silently drop its field value.
  const auto blobPath = SqliteRepository::blobDirectory((directory.path / "source" / "lexicon.db").string());
  fs::remove(fs::path(blobPath) / hash.substr(0, 2) / hash.substr(2));
  auto incomplete = lexicon::exchange::exportDocument(app, true);
  check(!incomplete && incomplete.error().message.find(hash) != std::string::npos,
        "export with files refuses a missing referenced Blob");
  check(lexicon::exchange::exportDocument(app, false).has_value(),
        "export without files still works when a referenced Blob is missing");

  if (failures == 0) std::cout << "exchange: all checks passed\n";
  return failures == 0 ? 0 : 1;
}
