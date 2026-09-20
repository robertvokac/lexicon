#include "LexiconApplication.h"
#include "SqliteRepository.h"

#include <filesystem>
#include <chrono>

#include <algorithm>
#include <iostream>

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
