#include "LexiconApplication.h"
#include "ImageValue.h"

#include <algorithm>
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

namespace lexicon {
Result<Neighborhood> LinkService::neighborhood(ItemId itemId, int depth, int maxNodes) {
  const auto summary = [&](ItemId id) -> Result<ItemRecord> {
    ItemColumnFilters byId;
    byId.id = std::to_string(id);
    auto rows = repository_.loadItems(-1, -1, {}, {}, byId, {}, {}, {}, -1, -1, -1, 1, 0, 0,
                                      SortOrder::Ascending);
    if (!rows)
      return std::unexpected(rows.error());
    if (rows->empty())
      return std::unexpected(Error{Error::Code::NotFound, "Item not found."});
    return std::move(rows->front());
  };
  Neighborhood graph;
  auto centre = summary(itemId);
  if (!centre)
    return std::unexpected(centre.error());
  graph.nodes.push_back({std::move(*centre), 0});
  std::set<int> visited{itemId};
  std::set<int> seenLinks;
  std::vector<LinkRecord> links;
  for (std::size_t next = 0; next < graph.nodes.size(); ++next) {
    const int id = graph.nodes[next].item.id;
    const int level = graph.nodes[next].depth;
    auto outgoing = repository_.loadLinks(id);
    if (!outgoing)
      return std::unexpected(outgoing.error());
    auto incoming = repository_.loadBacklinks(id);
    if (!incoming)
      return std::unexpected(incoming.error());
    outgoing->insert(outgoing->end(), incoming->begin(), incoming->end());
    for (auto &link : *outgoing) {
      if (!seenLinks.insert(link.id).second)
        continue;
      const int other = link.fromItemId == id ? link.toItemId : link.fromItemId;
      links.push_back(std::move(link));
      if (visited.contains(other) || level >= depth)
        continue;
      if (static_cast<int>(graph.nodes.size()) >= maxNodes) {
        graph.truncated = true;
        continue;
      }
      auto node = summary(other);
      if (!node)
        return std::unexpected(node.error());
      visited.insert(other);
      graph.nodes.push_back({std::move(*node), level + 1});
    }
  }
  for (auto &link : links)
    if (visited.contains(link.fromItemId) && visited.contains(link.toItemId))
      graph.edges.push_back(std::move(link));
  return graph;
}

Result<ItemRecord> ReviewService::review(ItemId id, ReviewRating rating) {
  auto item = repository_.loadItem(id);
  if (!item)
    return item;
  if (auto recorded = repository_.recordReview(id, levelAfterReview(item->understanding, rating)); !recorded)
    return std::unexpected(recorded.error());
  return repository_.loadItem(id);
}

std::string blobHashOf(const ItemFieldRecord &field, const std::string &value) {
  return storedFileHash(field.dataType, value);
}

namespace {
// Keeps a unit of work open for the lifetime of one export or import, and
// rolls it back unless it was committed.
class UnitOfWork {
public:
  explicit UnitOfWork(Repository &repository) : repository_(repository) {}
  ~UnitOfWork() {
    if (open_)
      (void)repository_.rollbackUnitOfWork();
  }
  UnitOfWork(const UnitOfWork &) = delete;
  UnitOfWork &operator=(const UnitOfWork &) = delete;
  Result<void> begin() {
    auto begun = repository_.beginUnitOfWork();
    open_ = begun.has_value();
    return begun;
  }
  Result<void> commit() {
    auto committed = repository_.commitUnitOfWork();
    if (committed)
      open_ = false;
    return committed;
  }

private:
  Repository &repository_;
  bool open_ = false;
};

std::string itemKey(int groupId, const std::string &title,
                    const std::string &disambiguation) {
  return std::to_string(groupId) + '\n' + trim(title) + '\n' + trim(disambiguation);
}

} // namespace

Result<DictionaryExport> ExchangeService::exportDictionary() {
  // The write lock keeps another program from changing the dictionary half
  // way through; nothing is written, and the unit is rolled back.
  UnitOfWork unit(repository_);
  if (auto begun = unit.begin(); !begun)
    return std::unexpected(begun.error());
  DictionaryExport dictionary;
  auto groups = repository_.loadGroups();
  if (!groups)
    return std::unexpected(groups.error());
  dictionary.groups = std::move(*groups);
  auto types = repository_.loadItemTypes(-1);
  if (!types)
    return std::unexpected(types.error());
  for (auto &type : *types) {
    auto fields = repository_.loadItemFields(type.id);
    if (!fields)
      return std::unexpected(fields.error());
    dictionary.types.push_back({std::move(type), std::move(*fields)});
  }
  auto rows = repository_.loadItems(-1, -1, {}, {}, {}, {}, {}, {}, -1, -1, -1,
                                    -1, 0, 0, SortOrder::Ascending);
  if (!rows)
    return std::unexpected(rows.error());
  for (const auto &row : *rows) {
    auto item = repository_.loadItem(row.id);
    if (!item)
      return std::unexpected(item.error());
    dictionary.items.push_back(std::move(*item));
    auto links = repository_.loadLinks(row.id);
    if (!links)
      return std::unexpected(links.error());
    for (auto &link : *links)
      dictionary.links.push_back(std::move(link));
  }
  auto alarms = repository_.loadAlarms();
  if (!alarms)
    return std::unexpected(alarms.error());
  dictionary.alarms = std::move(*alarms);
  return dictionary;
}

Result<ImportReport> ExchangeService::importDictionary(
    const DictionaryExport &dictionary,
    const std::map<std::string, std::string> &blobs) {
  ImportReport report;
  std::size_t omittedWarnings = 0;
  const auto warn = [&](std::string warning) {
    if (report.warnings.size() < 100)
      report.warnings.push_back(std::move(warning));
    else
      ++omittedWarnings;
  };
  UnitOfWork unit(repository_);
  if (auto begun = unit.begin(); !begun)
    return std::unexpected(begun.error());

  // Groups, by name.
  std::map<int, int> groupIds;
  {
    auto existing = repository_.loadGroups();
    if (!existing)
      return std::unexpected(existing.error());
    int nextPosition = 0;
    for (const auto &group : *existing)
      nextPosition = std::max(nextPosition, group.position + 1);
    for (const auto &group : dictionary.groups) {
      const auto name = trim(group.name);
      auto found = std::find_if(existing->begin(), existing->end(),
                                [&](const auto &candidate) { return candidate.name == name; });
      if (found == existing->end()) {
        if (auto stored = repository_.upsertGroup({-1, name, group.description, nextPosition++}); !stored)
          return std::unexpected(stored.error());
        auto reloaded = repository_.loadGroups();
        if (!reloaded)
          return std::unexpected(reloaded.error());
        *existing = std::move(*reloaded);
        found = std::find_if(existing->begin(), existing->end(),
                             [&](const auto &candidate) { return candidate.name == name; });
        if (found == existing->end())
          return std::unexpected(Error{Error::Code::Storage, "An imported group was not stored."});
        ++report.groupsCreated;
      }
      groupIds[group.id] = found->id;
    }
  }

  // Types and their fields, by name within their scope.
  std::map<int, int> typeIds;
  std::map<int, int> typeScopes; // new type ID -> group ID, -1 for all groups
  std::map<int, ItemFieldRecord> fieldsById; // new field ID -> field
  std::map<int, int> fieldIds;
  for (const auto &entry : dictionary.types) {
    int scope = -1;
    if (entry.type.groupId > 0) {
      const auto mapped = groupIds.find(entry.type.groupId);
      if (mapped == groupIds.end()) {
        warn("Type '" + entry.type.name + "' belongs to a group missing from the file; it was not imported.");
        continue;
      }
      scope = mapped->second;
    }
    const auto name = asciiFold(trim(entry.type.name));
    const auto findType = [&](const std::vector<ItemTypeRecord> &types) {
      return std::find_if(types.begin(), types.end(), [&](const auto &candidate) {
        return candidate.groupId == scope && asciiFold(candidate.name) == name;
      });
    };
    auto types = repository_.loadItemTypes(-1);
    if (!types)
      return std::unexpected(types.error());
    auto found = findType(*types);
    if (found == types->end()) {
      if (auto stored = repository_.upsertItemType({-1, scope, {}, trim(entry.type.name), entry.type.description}); !stored)
        return std::unexpected(stored.error());
      types = repository_.loadItemTypes(-1);
      if (!types)
        return std::unexpected(types.error());
      found = findType(*types);
      if (found == types->end())
        return std::unexpected(Error{Error::Code::Storage, "An imported type was not stored."});
      ++report.typesCreated;
    }
    const int typeId = found->id;
    typeIds[entry.type.id] = typeId;
    typeScopes[typeId] = scope;
    auto fields = repository_.loadItemFields(typeId);
    if (!fields)
      return std::unexpected(fields.error());
    int nextPosition = 0;
    for (const auto &field : *fields)
      nextPosition = std::max(nextPosition, field.position + 1);
    for (const auto &field : entry.fields) {
      const auto fieldName = asciiFold(trim(field.name));
      const auto findField = [&](const std::vector<ItemFieldRecord> &list) {
        return std::find_if(list.begin(), list.end(), [&](const auto &candidate) {
          return asciiFold(candidate.name) == fieldName;
        });
      };
      auto match = findField(*fields);
      if (match != fields->end() && match->dataType != field.dataType) {
        warn("Field '" + field.name + "' of type '" + entry.type.name +
             "' holds another kind of value here; its values were not imported.");
        continue;
      }
      if (match == fields->end()) {
        ItemFieldRecord created = field;
        created.id = -1;
        created.itemTypeId = typeId;
        created.name = trim(field.name);
        created.position = nextPosition++;
        if (auto stored = repository_.upsertItemField(created); !stored)
          return std::unexpected(stored.error());
        fields = repository_.loadItemFields(typeId);
        if (!fields)
          return std::unexpected(fields.error());
        match = findField(*fields);
        if (match == fields->end())
          return std::unexpected(Error{Error::Code::Storage, "An imported field was not stored."});
        ++report.fieldsCreated;
      }
      fieldIds[field.id] = match->id;
      fieldsById[match->id] = *match;
    }
  }

  // The files that travelled with the export.
  for (const auto &[hash, data] : blobs) {
    auto stored = repository_.importBlobData(data);
    if (!stored)
      return std::unexpected(stored.error());
    if (*stored != hash) {
      warn("A file in the export does not match its SHA-256 " + hash + "; it was left out.");
      continue;
    }
    ++report.blobsImported;
  }

  // Items, unless the group already has one of the same name.
  std::set<std::string> present;
  {
    auto rows = repository_.loadItems(-1, -1, {}, {}, {}, {}, {}, {}, -1, -1, -1,
                                      -1, 0, 0, SortOrder::Ascending);
    if (!rows)
      return std::unexpected(rows.error());
    for (const auto &row : *rows)
      present.insert(itemKey(row.groupId, row.title, row.disambiguation));
  }
  std::map<int, int> itemIds;
  std::set<int> created;
  int fallbackGroup = -1;
  for (const auto &source : dictionary.items) {
    int groupId = -1;
    if (const auto mapped = groupIds.find(source.groupId); mapped != groupIds.end()) {
      groupId = mapped->second;
    } else {
      if (fallbackGroup < 0) {
        auto resolved = repository_.defaultGroupId();
        if (!resolved)
          return std::unexpected(resolved.error());
        fallbackGroup = *resolved;
      }
      groupId = fallbackGroup;
      warn("Item '" + source.title + "' belongs to a group missing from the file; it was added to Default.");
    }
    const auto key = itemKey(groupId, source.title, source.disambiguation);
    if (present.contains(key)) {
      // Linked to below by what it is called, but otherwise left alone.
      auto existing = repository_.loadItems(groupId, -1, {}, {},
                                            {{}, trim(source.title), {}, {}}, {}, {}, {}, -1, -1, -1,
                                            -1, 0, 0, SortOrder::Ascending);
      if (!existing)
        return std::unexpected(existing.error());
      for (const auto &row : *existing)
        if (itemKey(row.groupId, row.title, row.disambiguation) == key)
          itemIds[source.id] = row.id;
      ++report.itemsSkipped;
      continue;
    }
    ItemRecord item = source;
    item.id = -1;
    item.revision = 0;
    item.groupId = groupId;
    item.itemTypeId = -1;
    item.fieldValues.clear();
    if (source.itemTypeId > 0) {
      const auto mapped = typeIds.find(source.itemTypeId);
      if (mapped == typeIds.end()) {
        warn("Item '" + source.title + "' has a type missing from the file; it was imported without one.");
      } else if (typeScopes[mapped->second] > 0 && typeScopes[mapped->second] != groupId) {
        warn("Item '" + source.title + "' has a type of another group; it was imported without one.");
      } else {
        item.itemTypeId = mapped->second;
      }
    }
    if (item.itemTypeId > 0) {
      int dropped = 0;
      for (const auto &[fieldId, value] : source.fieldValues) {
        const auto mapped = fieldIds.find(fieldId);
        if (mapped == fieldIds.end()) {
          ++dropped;
          continue;
        }
        const auto &field = fieldsById.at(mapped->second);
        if (field.itemTypeId != item.itemTypeId || !validFieldValue(field, value)) {
          ++dropped;
          continue;
        }
        if (const auto hash = blobHashOf(field, value); !hash.empty()) {
          auto blob = repository_.verifyBlob(hash);
          if (!blob || blob->type != BlobIssueType::Healthy) {
            warn("Item '" + source.title + "': the file of field '" + field.name +
                 "' is not in the export or in this database; the value was left out.");
            continue;
          }
        }
        item.fieldValues[mapped->second] = value;
      }
      if (dropped > 0)
        warn("Item '" + source.title + "': " + std::to_string(dropped) +
             " value(s) do not fit its fields here and were left out.");
    }
    auto id = repository_.saveItemReturningId(item);
    if (!id)
      return std::unexpected(Error{id.error().code, "Item '" + source.title + "': " + id.error().message});
    itemIds[source.id] = *id;
    created.insert(*id);
    present.insert(key);
    ++report.itemsCreated;
  }

  // Links that touch an imported item. Links between items that were already
  // here are theirs to keep as they are.
  for (const auto &source : dictionary.links) {
    const auto from = itemIds.find(source.fromItemId);
    const auto to = itemIds.find(source.toItemId);
    if (from == itemIds.end() || to == itemIds.end()) {
      warn("A link between items missing from the file was left out.");
      continue;
    }
    if (!created.contains(from->second) && !created.contains(to->second))
      continue;
    auto existing = repository_.loadLinks(from->second);
    if (!existing)
      return std::unexpected(existing.error());
    const bool duplicate = std::any_of(existing->begin(), existing->end(), [&](const auto &link) {
      return link.toItemId == to->second && link.linkType == source.linkType &&
             link.customValue == source.customValue;
    });
    if (duplicate)
      continue;
    LinkRecord link = source;
    link.id = -1;
    link.fromItemId = from->second;
    link.toItemId = to->second;
    if (auto stored = repository_.saveLink(link); !stored)
      return std::unexpected(stored.error());
    ++report.linksCreated;
  }

  // Alarms, unless one with the same title already goes off at that moment.
  if (!dictionary.alarms.empty()) {
    auto existing = repository_.loadAlarms();
    if (!existing)
      return std::unexpected(existing.error());
    for (const auto &source : dictionary.alarms) {
      const auto firesAt = normalizedUtcTime(source.firesAt);
      const bool present = std::any_of(existing->begin(), existing->end(), [&](const auto &alarm) {
        return alarm.title == trim(source.title) && alarm.firesAt == firesAt;
      });
      if (present)
        continue;
      AlarmRecord alarm = source;
      alarm.id = -1;
      if (auto saved = repository_.saveAlarm(alarm); !saved)
        return std::unexpected(Error{saved.error().code, "Alarm '" + source.title + "': " + saved.error().message});
      ++report.alarmsCreated;
    }
  }

  if (auto committed = unit.commit(); !committed)
    return std::unexpected(committed.error());
  if (omittedWarnings > 0)
    report.warnings.push_back("... and " + std::to_string(omittedWarnings) + " more.");
  return report;
}
} // namespace lexicon
