#pragma once
#include "Repository.h"
#include "Review.h"
#include "Validation.h"

namespace lexicon {
class ItemService {
public:
  explicit ItemService(Repository &repository) : repository_(repository) {}
  Result<std::vector<ItemRecord>> loadItems(
      int groupId, int typeId, const std::vector<ItemValueFilter> &valueFilters,
      const std::string &searchText, const ItemColumnFilters &columnFilters,
      const std::vector<ItemPropertyFilter> &propertyFilters,
      const std::string &tagFilter = {}, const std::string &flagFilter = {},
      int understandingFilter = -1, int statusFilter = -1,
      int pinnedFilter = -1, int limit = -1, int offset = 0, int sortColumn = 3,
      SortOrder sortOrder = SortOrder::Ascending) {
    return repository_.loadItems(
        groupId, typeId, valueFilters, searchText, columnFilters,
        propertyFilters, tagFilter, flagFilter, understandingFilter,
        statusFilter, pinnedFilter, limit, offset, sortColumn, sortOrder);
  }
  Result<int> countItems(int groupId, int typeId,
                         const std::vector<ItemValueFilter> &valueFilters,
                         const std::string &searchText,
                         const ItemColumnFilters &columnFilters,
                         const std::vector<ItemPropertyFilter> &propertyFilters,
                         const std::string &tagFilter = {},
                         const std::string &flagFilter = {},
                         int understandingFilter = -1, int statusFilter = -1,
                         int pinnedFilter = -1) {
    return repository_.countItems(groupId, typeId, valueFilters, searchText,
                                  columnFilters, propertyFilters, tagFilter,
                                  flagFilter, understandingFilter, statusFilter,
                                  pinnedFilter);
  }
  Result<ItemRecord> loadItem(ItemId id) { return repository_.loadItem(id); }
  Result<ItemId> createItem(const ItemRecord &item);
  Result<ItemId> createItem(const ItemRecord &item,
                            const std::vector<LinkRecord> &links,
                            const std::vector<LinkRecord> &backlinks = {});
  Result<void> saveItem(const ItemRecord &item);
  Result<void> deleteItem(ItemId id) { return repository_.deleteItem(id); }
  Result<void> logItemRead(ItemId id) { return repository_.logItemRead(id); }
  Result<ItemId> saveItemWithLinks(const ItemRecord &item,
                                   const std::vector<LinkRecord> &links,
                                   const std::vector<LinkRecord> &backlinks);

private:
  Repository &repository_;
};

class TypeService {
public:
  explicit TypeService(Repository &repository) : repository_(repository) {}
  Result<std::vector<ItemTypeRecord>> loadItemTypes(int groupId = -1) {
    return repository_.loadItemTypes(groupId);
  }
  Result<void> upsertItemType(const ItemTypeRecord &itemType) {
    if (auto valid = validateType(itemType); !valid)
      return valid;
    return repository_.upsertItemType(itemType);
  }
  Result<int> countItemsForType(int itemTypeId) {
    return repository_.countItemsForType(itemTypeId);
  }
  Result<void> deleteItemType(int itemTypeId) {
    return repository_.deleteItemType(itemTypeId);
  }
  Result<std::vector<ItemFieldRecord>> loadItemFields(int itemTypeId) {
    return repository_.loadItemFields(itemTypeId);
  }
  Result<void> upsertItemField(const ItemFieldRecord &field) {
    if (auto valid = validateField(field); !valid)
      return valid;
    return repository_.upsertItemField(field);
  }
  Result<int> countFieldValues(int fieldId) {
    return repository_.countFieldValues(fieldId);
  }
  Result<void> deleteItemField(int fieldId) {
    return repository_.deleteItemField(fieldId);
  }

private:
  Repository &repository_;
};

class GroupService {
public:
  explicit GroupService(Repository &repository) : repository_(repository) {}
  Result<std::vector<GroupRecord>> loadGroups() {
    return repository_.loadGroups();
  }
  Result<int> defaultGroupId() { return repository_.defaultGroupId(); }
  Result<void> upsertGroup(const GroupRecord &group) {
    if (auto valid = validateGroup(group); !valid)
      return valid;
    return repository_.upsertGroup(group);
  }
  Result<void> deleteGroup(int id) { return repository_.deleteGroup(id); }

private:
  Repository &repository_;
};

// The items around one item, following links in both directions.
struct GraphNode {
  ItemRecord item; // a list row: no content, values or properties
  int depth = 0;   // links away from the centre
};
struct Neighborhood {
  std::vector<GraphNode> nodes; // the centre first, then by depth
  std::vector<LinkRecord> edges; // the links among these nodes
  bool truncated = false;        // more items were in reach than allowed
};

class LinkService {
public:
  explicit LinkService(Repository &repository) : repository_(repository) {}
  // Breadth first from `itemId`, up to `depth` links away and at most
  // `maxNodes` items.
  Result<Neighborhood> neighborhood(ItemId itemId, int depth, int maxNodes);
  Result<std::vector<LinkRecord>> loadLinks(int itemId) {
    return repository_.loadLinks(itemId);
  }
  Result<std::vector<LinkRecord>> loadBacklinks(int itemId) {
    return repository_.loadBacklinks(itemId);
  }
  Result<void> saveLink(const LinkRecord &link) {
    if (auto valid = validateLink(link); !valid)
      return valid;
    return repository_.saveLink(link);
  }
  Result<void> deleteLink(int linkId) { return repository_.deleteLink(linkId); }

private:
  Repository &repository_;
};

class SearchService {
public:
  explicit SearchService(Repository &repository) : repository_(repository) {}
  Result<std::vector<std::string>> loadSuggestions() {
    return repository_.loadSuggestions();
  }
  Result<std::vector<std::string>> loadItemTitles() {
    return repository_.loadItemTitles();
  }
  Result<std::vector<UsageValueRecord>> loadTagUsage() {
    return repository_.loadTagUsage();
  }
  Result<std::vector<UsageValueRecord>> loadFlagUsage() {
    return repository_.loadFlagUsage();
  }
  Result<std::vector<UsageValueRecord>> loadAliasUsage() {
    return repository_.loadAliasUsage();
  }
  Result<int> findItemId(const std::string &title,
                         const std::string &disambiguation = {}) {
    return repository_.findItemId(title, disambiguation);
  }

private:
  Repository &repository_;
};

class ConfigurationService {
public:
  explicit ConfigurationService(Repository &repository)
      : repository_(repository) {}
  Result<std::map<std::string, std::string>> loadConfiguration() {
    return repository_.loadConfiguration();
  }
  Result<void>
  saveConfiguration(const std::map<std::string, std::string> &values) {
    return repository_.saveConfiguration(values);
  }

private:
  Repository &repository_;
};

class BlobService {
public:
  explicit BlobService(Repository &repository) : repository_(repository) {}
  Result<std::string> importFile(const std::string &path) {
    return repository_.importBlob(path);
  }
  Result<void> exportFile(const std::string &hash, const std::string &path) {
    return repository_.exportBlob(hash, path);
  }
  Result<std::string> importData(const std::string &data) {
    return repository_.importBlobData(data);
  }
  Result<std::string> readData(const std::string &hash) {
    return repository_.readBlobData(hash);
  }
  Result<BlobMaintenanceReport> scanStorage(
      BlobScanDepth depth = BlobScanDepth::Structural) {
    return repository_.scanBlobStorage(depth);
  }
  Result<BlobGarbageCollectionResult> collectUnusedBlobs(
      const BlobMaintenanceReport &scan) {
    return repository_.collectUnusedBlobs(scan);
  }
  Result<BlobIssue> verifyBlob(const std::string &hash) {
    return repository_.verifyBlob(hash);
  }

private:
  Repository &repository_;
};

class ReviewService {
public:
  explicit ReviewService(Repository &repository) : repository_(repository) {}
  // The items due for review now; groupId <= 0 means every group.
  Result<std::vector<ItemRecord>> queue(int groupId, int limit) {
    return repository_.loadReviewQueue(groupId, limit);
  }
  Result<int> countDue(int groupId) { return repository_.countDueItems(groupId); }
  // Moves the item's understanding by the rating, records the review, and
  // returns the item as it is now, with its next due date.
  Result<ItemRecord> review(ItemId id, ReviewRating rating);

private:
  Repository &repository_;
};

// A whole dictionary as it travels between databases. The IDs are those of
// the exporting database; they only connect the records of one export.
struct TypeExport {
  ItemTypeRecord type;
  std::vector<ItemFieldRecord> fields;
};
struct DictionaryExport {
  std::vector<GroupRecord> groups;
  std::vector<TypeExport> types;
  std::vector<ItemRecord> items;
  std::vector<LinkRecord> links;
};
struct ImportReport {
  int groupsCreated = 0;
  int typesCreated = 0;
  int fieldsCreated = 0;
  int itemsCreated = 0;
  // Items already present: same group, title and disambiguation.
  int itemsSkipped = 0;
  int linksCreated = 0;
  int blobsImported = 0;
  // What could not be imported as it was, in words.
  std::vector<std::string> warnings;
};

// The hash of the Blob a field value refers to, or empty for other values.
std::string blobHashOf(const ItemFieldRecord &field, const std::string &value);

class ExchangeService {
public:
  explicit ExchangeService(Repository &repository) : repository_(repository) {}
  // One consistent snapshot of every group, type, field, item and link.
  Result<DictionaryExport> exportDictionary();
  // Merges an export into this database in one unit of work: groups, types
  // and fields are matched by name, items already present are left alone,
  // and links are added where they touch an imported item. `blobs` holds the
  // file contents that travelled with the export, by SHA-256.
  Result<ImportReport> importDictionary(
      const DictionaryExport &dictionary,
      const std::map<std::string, std::string> &blobs);

private:
  Repository &repository_;
};

class LexiconApplication {
public:
  explicit LexiconApplication(Repository &repository)
      : items(repository), types(repository), groups(repository),
        links(repository), search(repository), configuration(repository),
        blobs(repository), exchange(repository), review(repository) {}
  ItemService items;
  TypeService types;
  GroupService groups;
  LinkService links;
  SearchService search;
  ConfigurationService configuration;
  BlobService blobs;
  ExchangeService exchange;
  ReviewService review;
};
} // namespace lexicon
