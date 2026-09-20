#include "LexiconApplication.h"

#include <set>

namespace lexicon {
namespace {
Result<void> validateForSave(Repository &repository, const ItemRecord &item) {
  std::vector<ItemFieldRecord> fields;
  if (item.itemTypeId > 0) {
    auto loaded = repository.loadItemFields(item.itemTypeId);
    if (!loaded)
      return std::unexpected(loaded.error());
    fields = std::move(*loaded);
  }
  return validateItem(item, fields);
}

Result<void> syncLinks(Repository &repository, int itemId,
                       std::vector<LinkRecord> desired, bool outgoing) {
  auto existing = outgoing ? repository.loadLinks(itemId)
                           : repository.loadBacklinks(itemId);
  if (!existing)
    return std::unexpected(existing.error());
  std::set<int> ownedIds;
  std::set<int> seenIds;
  for (const auto &old : *existing)
    ownedIds.insert(old.id);
  for (const auto &link : desired) {
    if (link.id < 0)
      continue;
    if (!ownedIds.contains(link.id) || !seenIds.insert(link.id).second)
      return std::unexpected(
          Error{Error::Code::Validation,
                "Link does not belong to this item or occurs twice."});
  }
  for (const auto &old : *existing) {
    bool kept = false;
    for (const auto &current : desired) {
      if (current.id == old.id) {
        kept = true;
        break;
      }
    }
    if (!kept) {
      auto deleted = repository.deleteLink(old.id);
      if (!deleted)
        return deleted;
    }
  }
  for (auto &link : desired) {
    if (outgoing)
      link.fromItemId = itemId;
    else
      link.toItemId = itemId;
    if (auto valid = validateLink(link); !valid)
      return valid;
    if (auto saved = repository.saveLink(link); !saved)
      return saved;
  }
  return {};
}
} // namespace

Result<void> ItemService::saveItem(const ItemRecord &item) {
  if (auto valid = validateForSave(repository_, item); !valid)
    return valid;
  return repository_.saveItem(item);
}

Result<ItemId> ItemService::createItem(const ItemRecord &item) {
  return createItem(item, {}, {});
}

Result<ItemId> ItemService::createItem(
    const ItemRecord &item, const std::vector<LinkRecord> &links,
    const std::vector<LinkRecord> &backlinks) {
  if (item.id >= 0)
    return std::unexpected(Error{Error::Code::Validation,
                                 "A new item must not already have an ID."});
  return saveItemWithLinks(item, links, backlinks);
}

Result<ItemId>
ItemService::saveItemWithLinks(const ItemRecord &item,
                               const std::vector<LinkRecord> &links,
                               const std::vector<LinkRecord> &backlinks) {
  if (auto valid = validateForSave(repository_, item); !valid)
    return std::unexpected(valid.error());
  if (auto begin = repository_.beginUnitOfWork(); !begin)
    return std::unexpected(begin.error());
  const auto rollback = [&](Error error) -> Result<ItemId> {
    if (auto rolledBack = repository_.rollbackUnitOfWork(); !rolledBack)
      return std::unexpected(Error{
          Error::Code::Storage,
          "Operation failed: " + error.message + "; rollback failed: " +
              rolledBack.error().message});
    return std::unexpected(std::move(error));
  };
  auto saved = repository_.saveItemReturningId(item);
  if (!saved)
    return rollback(saved.error());
  if (auto outgoing = syncLinks(repository_, *saved, links, true); !outgoing)
    return rollback(outgoing.error());
  if (auto incoming = syncLinks(repository_, *saved, backlinks, false);
      !incoming)
    return rollback(incoming.error());
  if (auto commit = repository_.commitUnitOfWork(); !commit)
    return rollback(commit.error());
  return *saved;
}
} // namespace lexicon
