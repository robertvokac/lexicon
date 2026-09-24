#include "LexiconApplication.h"
#include "SqliteRepository.h"

#include <filesystem>
#include <chrono>

#include <algorithm>
#include <iostream>
#include <vector>

namespace {
template <class T>
bool check(const lexicon::Result<T> &result, const char *operation) {
  if (result)
    return true;
  std::cerr << operation << ": " << result.error().message << '\n';
  return false;
}
bool condition(bool ok, const char *message) {
  if (!ok)
    std::cerr << message << '\n';
  return ok;
}
} // namespace

int main() {
  namespace fs = std::filesystem;
  const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory = fs::temp_directory_path() / ("lexicon-integration-" + std::to_string(unique));
  fs::create_directories(directory);
  struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{directory};
  const auto path = (directory / "lexicon.db").string();
  SqliteRepository repository;
  if (!check(repository.open(path),
             "Database migration"))
    return 1;
  lexicon::LexiconApplication application(repository);

  auto groupId = application.groups.defaultGroupId();
  if (!check(groupId, "Default group") ||
      !condition(*groupId > 0, "Invalid group ID"))
    return 1;

  lexicon::ItemRecord first;
  first.groupId = *groupId;
  first.title = "Pointer provenance";
  auto firstId = application.items.createItem(first);
  if (!check(firstId, "Create first item") ||
      !condition(*firstId > 0, "Invalid first ID"))
    return 1;

  lexicon::ItemRecord second;
  second.groupId = *groupId;
  second.title = "C++";
  auto secondId = application.items.saveItemWithLinks(second, {}, {});
  if (!check(secondId, "Create second item") ||
      !condition(*secondId > *firstId, "Invalid second ID"))
    return 1;

  first.id = *firstId;
  lexicon::LinkRecord relation;
  relation.toItemId = *secondId;
  relation.linkType = lexicon::LinkType::PartOf;
  if (!check(application.items.saveItemWithLinks(first, {relation}, {}),
             "Save item with link"))
    return 1;
  auto links = application.links.loadLinks(*firstId);
  if (!check(links, "Load links") ||
      !condition(links->size() == 1 && links->front().toItemId == *secondId,
                 "Link was not persisted"))
    return 1;

  first.title = "Should roll back";
  lexicon::LinkRecord invalid;
  invalid.toItemId = -1;
  auto failed = application.items.saveItemWithLinks(first, {invalid}, {});
  if (!condition(!failed &&
                     failed.error().code == lexicon::Error::Code::Validation,
                 "Invalid link was not rejected"))
    return 1;
  auto persisted = application.items.loadItem(*firstId);
  auto remainingLinks = application.links.loadLinks(*firstId);
  if (!check(persisted, "Reload item") ||
      !check(remainingLinks, "Reload links") ||
      !condition(persisted->title == "Pointer provenance" &&
                     remainingLinks->size() == 1,
                 "Transaction was not rolled back"))
    return 1;

  lexicon::ItemTypeRecord type;
  type.name = "Term";
  if (!check(application.types.upsertItemType(type), "Create type"))
    return 1;
  auto types = application.types.loadItemTypes();
  if (!check(types, "Load types"))
    return 1;
  const auto termType =
      std::find_if(types->begin(), types->end(), [](const auto &candidate) {
        return candidate.name == "Term";
      });
  if (!condition(termType != types->end(), "Created type missing"))
    return 1;

  lexicon::ItemFieldRecord field;
  field.itemTypeId = termType->id;
  field.name = "Meaning";
  if (!check(application.types.upsertItemField(field), "Create field"))
    return 1;
  auto fields = application.types.loadItemFields(termType->id);
  if (!check(fields, "Load fields") ||
      !condition(fields->size() == 1, "Created field missing"))
    return 1;

  lexicon::ItemRecord localized;
  localized.groupId = *groupId;
  localized.itemTypeId = termType->id;
  localized.title = "Příliš žluťoučký kůň";
  localized.disambiguation = "česky";
  localized.aliases = {"kůň"};
  localized.tags = {"čeština"};
  localized.properties = {{"poznámka", "hodnota"}};
  localized.fieldValues[fields->front().id] = "Význam";
  localized.content = "Žádný problém.";
  auto localizedId = application.items.createItem(localized);
  if (!check(localizedId, "Create UTF-8 item"))
    return 1;
  auto loaded = application.items.loadItem(*localizedId);
  if (!check(loaded, "Load UTF-8 item") ||
      !condition(loaded->title == localized.title &&
                     loaded->disambiguation == localized.disambiguation &&
                     loaded->aliases == localized.aliases &&
                     loaded->tags == localized.tags &&
                     loaded->properties.front().value == "hodnota" &&
                     loaded->fieldValues == localized.fieldValues &&
                     loaded->content == localized.content,
                 "UTF-8 record did not round trip"))
    return 1;
  auto found =
      application.search.findItemId(localized.title, localized.disambiguation);
  if (!check(found, "Find UTF-8 item") ||
      !condition(*found == *localizedId, "UTF-8 lookup failed"))
    return 1;

  // The Inbox: an idea goes to Default with the type Inbox, which the first
  // idea creates, available in all groups, and every later one reuses.
  const auto inboxTypes = [&] {
    std::vector<lexicon::ItemTypeRecord> matches;
    for (const auto &candidate : application.types.loadItemTypes(-1).value_or(std::vector<lexicon::ItemTypeRecord>{}))
      if (candidate.name == "Inbox") matches.push_back(candidate);
    return matches;
  };
  if (!condition(inboxTypes().empty(), "A fresh database already has an Inbox type"))
    return 1;
  auto blank = application.inbox.capture("  ", "No title.");
  if (!condition(!blank && blank.error().code == lexicon::Error::Code::Validation,
                 "An idea without a title was accepted") ||
      !condition(inboxTypes().empty(), "A refused idea left an Inbox type behind"))
    return 1;
  auto idea = application.inbox.capture("Lock-free queue", "Try a ring buffer.\nMeasure it first.");
  if (!check(idea, "Capture an idea") ||
      !condition(idea->groupId == *groupId && idea->groupName == "Default", "An idea did not go to Default") ||
      !condition(idea->itemTypeName == "Inbox" && idea->content == "Try a ring buffer.\nMeasure it first.",
                 "An idea did not get the Inbox type or its text"))
    return 1;
  const auto inbox = inboxTypes();
  if (!condition(inbox.size() == 1 && inbox[0].groupId <= 0 && inbox[0].id == idea->itemTypeId,
                 "The Inbox type was not created once, available in all groups"))
    return 1;
  auto nextIdea = application.inbox.capture("Arena allocator", "");
  if (!check(nextIdea, "Capture a second idea") ||
      !condition(nextIdea->itemTypeId == idea->itemTypeId && inboxTypes().size() == 1,
                 "A second idea did not reuse the Inbox type"))
    return 1;
  auto twin = application.inbox.capture("Lock-free queue", "Again.");
  if (!condition(!twin && twin.error().code == lexicon::Error::Code::Validation,
                 "An idea with a title already in Default was accepted"))
    return 1;
  // The idea moves on to another group and keeps its type.
  if (!check(application.groups.upsertGroup({-1, "C++", "", 5}), "Create C++"))
    return 1;
  int cpp = -1;
  for (const auto &group : application.groups.loadGroups().value_or(std::vector<lexicon::GroupRecord>{}))
    if (group.name == "C++") cpp = group.id;
  auto moved = *application.items.loadItem(idea->id);
  moved.groupId = cpp;
  if (!check(application.items.saveItem(moved), "Move an idea to another group") ||
      !condition(application.items.loadItem(idea->id)->itemTypeName == "Inbox", "A moved idea lost its type"))
    return 1;

  // An Inbox type someone made already - any case, in Default - is the one used.
  {
    const auto otherPath = (directory / "inbox.db").string();
    SqliteRepository otherRepository;
    if (!check(otherRepository.open(otherPath), "Open a second database"))
      return 1;
    lexicon::LexiconApplication other(otherRepository);
    const int otherDefault = other.groups.defaultGroupId().value_or(-1);
    if (!check(other.types.upsertItemType({-1, otherDefault, {}, "inbox", "Mine."}), "Create a Default inbox type"))
      return 1;
    auto mine = other.inbox.capture("Kept in my type", "");
    const auto all = other.types.loadItemTypes(-1).value_or(std::vector<lexicon::ItemTypeRecord>{});
    if (!check(mine, "Capture into an existing type") ||
        !condition(mine->itemTypeName == "inbox" && all.size() == 1, "An existing inbox type was not reused"))
      return 1;
  }

  const std::map<std::string, std::string> settings{{"view.theme", "tmavý"}};
  if (!check(application.configuration.saveConfiguration(settings),
             "Save configuration"))
    return 1;
  auto configuration = application.configuration.loadConfiguration();
  if (!check(configuration, "Load configuration") ||
      !condition(configuration->at("view.theme") == "tmavý",
                 "Configuration did not round trip"))
    return 1;
  return 0;
}
