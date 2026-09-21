#include "LexiconApplication.h"
#include "SqliteRepository.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

namespace {
namespace fs = std::filesystem;
template <class T>
bool success(const lexicon::Result<T> &result, const char *operation) {
  if (result) return true;
  std::cerr << operation << ": " << result.error().message << '\n';
  return false;
}
template <class T>
bool notFound(const lexicon::Result<T> &result, const char *operation) {
  if (!result && result.error().code == lexicon::Error::Code::NotFound) return true;
  std::cerr << operation << " did not return NotFound\n";
  return false;
}
bool check(bool condition, const char *message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}
struct TemporaryDirectory {
  fs::path path = fs::temp_directory_path() /
      ("lexicon-correctness-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  TemporaryDirectory() { fs::create_directories(path); }
  ~TemporaryDirectory() { std::error_code error; fs::remove_all(path, error); }
};
} // namespace

int main() {
  TemporaryDirectory directory;
  const auto path = (directory.path / "lexicon.db").string();
  int defaultId = -1;
  int firstId = -1;
  {
    SqliteRepository repository;
    if (!success(repository.open(path), "Open fresh database")) return 1;
    lexicon::LexiconApplication app(repository);
    auto groups = app.groups.loadGroups();
    auto resolved = app.groups.defaultGroupId();
    if (!success(groups, "Load fresh groups") ||
        !success(resolved, "Resolve Default group")) return 1;
    defaultId = *resolved;
    const auto current = std::find_if(groups->begin(), groups->end(),
        [defaultId](const auto &group) { return group.id == defaultId; });
    if (!check(current != groups->end() && current->name == "Default",
               "Fresh database did not provide Default") ||
        !check(std::none_of(groups->begin(), groups->end(),
                  [](const auto &group) { return group.name == "Inbox"; }),
               "Fresh database created an Inbox")) return 1;
    // The desktop resolves this ID when no Group is selected, before creating an Item.
    lexicon::ItemRecord first;
    first.groupId = defaultId;
    first.title = "Created without a selected Group";
    auto created = app.items.createItem(first);
    if (!success(created, "Create Item using default selection")) return 1;
    firstId = *created;
    auto loaded = app.items.loadItem(firstId);
    if (!success(loaded, "Load default-group Item") ||
        !check(loaded->groupId == defaultId && loaded->groupName == "Default",
               "New Item did not use Default")) return 1;
    first = *loaded;
    if (!success(app.groups.upsertGroup(*current), "Update Group with identical values") ||
        !success(app.items.saveItem(first), "Update Item with identical values")) return 1;

    // A second Item with the same title in the same Group is refused in words,
    // not with the unique index's SQL message; a disambiguation tells them apart.
    lexicon::ItemRecord twin;
    twin.groupId = defaultId;
    twin.title = "  Created without a selected Group ";
    auto refused = app.items.createItem(twin);
    if (!check(!refused && refused.error().code == lexicon::Error::Code::Validation &&
                   refused.error().message.find("already exists in this group") != std::string::npos,
               "A twin Item was not refused with a validation message")) return 1;
    twin.disambiguation = "second";
    auto distinct = app.items.createItem(twin);
    if (!success(distinct, "Create twin with a disambiguation")) return 1;
    auto renamed = app.items.loadItem(*distinct);
    if (!success(renamed, "Load disambiguated twin")) return 1;
    renamed->disambiguation.clear();
    auto collision = app.items.saveItem(*renamed);
    if (!check(!collision && collision.error().code == lexicon::Error::Code::Validation,
               "Renaming onto an existing Item was not refused") ||
        !success(app.items.deleteItem(*distinct), "Delete disambiguated twin")) return 1;

    lexicon::ItemTypeRecord type;
    type.name = "Example";
    if (!success(app.types.upsertItemType(type), "Create Type")) return 1;
    auto types = app.types.loadItemTypes();
    if (!success(types, "Load Types")) return 1;
    const auto storedType = std::find_if(types->begin(), types->end(),
        [](const auto &candidate) { return candidate.name == "Example"; });
    if (!check(storedType != types->end(), "Created Type missing") ||
        !success(app.types.upsertItemType(*storedType),
                 "Update Type with identical values")) return 1;
    lexicon::ItemFieldRecord field;
    field.itemTypeId = storedType->id;
    field.name = "Text";
    if (!success(app.types.upsertItemField(field), "Create Field")) return 1;
    auto fields = app.types.loadItemFields(storedType->id);
    if (!success(fields, "Load Fields") ||
        !check(fields->size() == 1, "Created Field missing") ||
        !success(app.types.upsertItemField(fields->front()),
                 "Update Field with identical values")) return 1;

    lexicon::ItemRecord second;
    second.groupId = defaultId;
    second.title = "Link target";
    auto secondId = app.items.createItem(second);
    if (!success(secondId, "Create second Item")) return 1;
    lexicon::LinkRecord link;
    link.fromItemId = firstId;
    link.toItemId = *secondId;
    link.linkType = lexicon::LinkType::Custom;
    link.customValue = "  Written By  ";
    if (!success(app.links.saveLink(link), "Save Custom link")) return 1;
    auto links = app.links.loadLinks(firstId);
    if (!success(links, "Load Custom link") ||
        !check(links->size() == 1 && links->front().customValue == "Written By" &&
                   links->front().linkType == lexicon::LinkType::Custom,
               "Custom link did not round trip")) return 1;
    link = links->front();
    if (!success(app.links.saveLink(link), "Update Link with identical values")) return 1;
    link.linkType = lexicon::LinkType::Related;
    link.customValue = "stale custom value";
    if (!success(app.links.saveLink(link), "Normalize ordinary link")) return 1;
    links = app.links.loadLinks(firstId);
    if (!success(links, "Reload ordinary link") ||
        !check(links->size() == 1 && links->front().customValue.empty(),
               "Ordinary link retained stale custom value")) return 1;
    lexicon::LinkRecord invalid = link;
    invalid.id = -1;
    invalid.linkType = lexicon::LinkType::Custom;
    invalid.customValue = "  ";
    auto emptyCustom = app.links.saveLink(invalid);
    invalid.linkType = lexicon::LinkType::None;
    invalid.customValue = "";
    auto none = app.links.saveLink(invalid);
    invalid.linkType = static_cast<lexicon::LinkType>(999);
    auto unknown = app.links.saveLink(invalid);
    if (!check(!emptyCustom && emptyCustom.error().code == lexicon::Error::Code::Validation &&
               !none && none.error().code == lexicon::Error::Code::Validation &&
               !unknown && unknown.error().code == lexicon::Error::Code::Validation,
               "Invalid link type/value was accepted")) return 1;

    constexpr int missing = 999999;
    lexicon::GroupRecord missingGroup{missing, "Missing", "", 0};
    lexicon::ItemTypeRecord missingType;
    missingType.id = missing;
    missingType.name = "Missing";
    lexicon::ItemFieldRecord missingField;
    missingField.id = missing;
    missingField.itemTypeId = storedType->id;
    missingField.name = "Missing";
    lexicon::ItemRecord missingItem = first;
    missingItem.id = missing;
    lexicon::LinkRecord missingLink = link;
    missingLink.id = missing;
    if (!notFound(app.groups.upsertGroup(missingGroup), "Update missing Group") ||
        !notFound(app.groups.deleteGroup(missing), "Delete missing Group") ||
        !notFound(app.types.upsertItemType(missingType), "Update missing Type") ||
        !notFound(app.types.deleteItemType(missing), "Delete missing Type") ||
        !notFound(app.types.upsertItemField(missingField), "Update missing Field") ||
        !notFound(app.types.deleteItemField(missing), "Delete missing Field") ||
        !notFound(app.items.saveItem(missingItem), "Update missing Item") ||
        !notFound(app.items.deleteItem(missing), "Delete missing Item") ||
        !notFound(app.links.saveLink(missingLink), "Update missing Link") ||
        !notFound(app.links.deleteLink(missing), "Delete missing Link") ||
        !notFound(app.items.logItemRead(missing), "Read-log missing Item")) return 1;
    if (!success(app.items.logItemRead(firstId), "Read-log existing Item")) return 1;

    if (!success(repository.beginUnitOfWork(), "Begin explicit unit")) return 1;
    lexicon::ItemRecord staged;
    staged.groupId = defaultId;
    staged.title = "Rolled back Item";
    auto stagedId = repository.saveItemReturningId(staged);
    if (!success(stagedId, "Insert staged Item") ||
        !success(repository.rollbackUnitOfWork(), "Explicit rollback")) return 1;
    if (!notFound(repository.loadItem(*stagedId), "Load rolled-back Item") ||
        !success(repository.beginUnitOfWork(), "Begin after rollback") ||
        !success(repository.rollbackUnitOfWork(), "Second explicit rollback")) return 1;
  }
  {
    SqliteRepository repository;
    if (!success(repository.open(path), "Reopen database")) return 1;
    lexicon::LexiconApplication app(repository);
    auto groupId = app.groups.defaultGroupId();
    auto item = app.items.loadItem(firstId);
    if (!success(groupId, "Resolve Default after reopen") ||
        !success(item, "Load Item after reopen") ||
        !check(*groupId == defaultId && item->groupName == "Default",
               "Default did not survive reopen")) return 1;
    if (!success(app.groups.deleteGroup(*groupId), "Delete Default for recreation test")) return 1;
    auto recreated = app.groups.defaultGroupId();
    auto groups = app.groups.loadGroups();
    if (!success(recreated, "Recreate Default") ||
        !success(groups, "Load recreated Default") ||
        !check(std::any_of(groups->begin(), groups->end(),
                           [&](const auto &group) {
                             return group.id == *recreated && group.name == "Default";
                           }), "defaultGroupId did not recreate Default")) return 1;
  }
  return 0;
}
