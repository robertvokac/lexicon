#pragma once
#include "Records.h"
#include "Result.h"
#include "BlobMaintenance.h"

namespace lexicon {
class Repository {
public:
  virtual ~Repository() = default;
  virtual Result<std::map<std::string, std::string>> loadConfiguration() = 0;
  virtual Result<void>
  saveConfiguration(const std::map<std::string, std::string> &values) = 0;
  virtual Result<std::vector<GroupRecord>> loadGroups() = 0;
  virtual Result<int> defaultGroupId() = 0;
  virtual Result<void> upsertGroup(const GroupRecord &group) = 0;
  virtual Result<void> deleteGroup(int groupId) = 0;
  virtual Result<std::vector<ItemTypeRecord>> loadItemTypes(int groupId) = 0;
  virtual Result<void> upsertItemType(const ItemTypeRecord &itemType) = 0;
  virtual Result<int> countItemsForType(int itemTypeId) = 0;
  virtual Result<void> deleteItemType(int itemTypeId) = 0;
  virtual Result<std::vector<ItemFieldRecord>>
  loadItemFields(int itemTypeId) = 0;
  virtual Result<void> upsertItemField(const ItemFieldRecord &field) = 0;
  virtual Result<int> countFieldValues(int fieldId) = 0;
  virtual Result<void> deleteItemField(int fieldId) = 0;
  virtual Result<std::vector<ItemRecord>> loadItems(
      int groupId, int typeId, const std::vector<ItemValueFilter> &valueFilters,
      const std::string &searchText, const ItemColumnFilters &columnFilters,
      const std::vector<ItemPropertyFilter> &propertyFilters,
      const std::string &tagFilter, const std::string &flagFilter,
      int understandingFilter, int statusFilter, int pinnedFilter, int limit,
      int offset, int sortColumn, SortOrder sortOrder) = 0;
  virtual Result<int> countItems(
      int groupId, int typeId, const std::vector<ItemValueFilter> &valueFilters,
      const std::string &searchText, const ItemColumnFilters &columnFilters,
      const std::vector<ItemPropertyFilter> &propertyFilters,
      const std::string &tagFilter, const std::string &flagFilter,
      int understandingFilter, int statusFilter, int pinnedFilter) = 0;
  virtual Result<ItemRecord> loadItem(int itemId) = 0;
  virtual Result<void> saveItem(const ItemRecord &item) = 0;
  virtual Result<int> saveItemReturningId(const ItemRecord &item) = 0;
  virtual Result<void> deleteItem(int itemId) = 0;
  virtual Result<std::vector<LinkRecord>> loadLinks(int itemId) = 0;
  virtual Result<std::vector<LinkRecord>> loadBacklinks(int itemId) = 0;
  virtual Result<void> saveLink(const LinkRecord &link) = 0;
  virtual Result<void> deleteLink(int linkId) = 0;
  virtual Result<void> logItemRead(int itemId) = 0;
  // The items due for review, as list rows: those reviewed before, the most
  // overdue first, then those never reviewed. groupId <= 0 means all groups.
  virtual Result<std::vector<ItemRecord>> loadReviewQueue(int groupId, int limit) = 0;
  virtual Result<int> countDueItems(int groupId) = 0;
  // Stores the understanding a review left the item at and the review time.
  virtual Result<void> recordReview(int itemId, UnderstandingLevel level) = 0;
  virtual Result<std::vector<std::string>> loadSuggestions() = 0;
  virtual Result<std::vector<std::string>> loadItemTitles() = 0;
  virtual Result<std::vector<UsageValueRecord>> loadTagUsage() = 0;
  virtual Result<std::vector<UsageValueRecord>> loadFlagUsage() = 0;
  virtual Result<std::vector<UsageValueRecord>> loadAliasUsage() = 0;
  virtual Result<int> findItemId(const std::string &title,
                                 const std::string &disambiguation) = 0;
  virtual Result<void> beginUnitOfWork() = 0;
  virtual Result<void> commitUnitOfWork() = 0;
  virtual Result<void> rollbackUnitOfWork() = 0;
  virtual Result<std::string> importBlob(const std::string &sourcePath) = 0;
  // The same for bytes held in memory, such as a file carried in an export.
  virtual Result<std::string> importBlobData(const std::string &data) = 0;
  // The verified contents of a stored Blob.
  virtual Result<std::string> readBlobData(const std::string &hash) = 0;
  virtual Result<void> exportBlob(const std::string &hash,
                                  const std::string &destinationPath) = 0;
  virtual Result<BlobMaintenanceReport> scanBlobStorage(BlobScanDepth depth) = 0;
  virtual Result<BlobGarbageCollectionResult> collectUnusedBlobs(
      const BlobMaintenanceReport &scan) = 0;
  virtual Result<BlobIssue> verifyBlob(const std::string &hash) = 0;
};
} // namespace lexicon
