#pragma once
#include "Repository.h"
#include <memory>

class SqliteRepository final : public lexicon::Repository {
  struct Impl;
  std::unique_ptr<Impl> impl_;
public:
  SqliteRepository();
  ~SqliteRepository() override;
  SqliteRepository(const SqliteRepository&) = delete;
  SqliteRepository& operator=(const SqliteRepository&) = delete;
  template <class T> using Result = lexicon::Result<T>;
  using GroupRecord = lexicon::GroupRecord;
  using ItemTypeRecord = lexicon::ItemTypeRecord;
  using ItemFieldRecord = lexicon::ItemFieldRecord;
  using ItemValueFilter = lexicon::ItemValueFilter;
  using ItemColumnFilters = lexicon::ItemColumnFilters;
  using ItemPropertyFilter = lexicon::ItemPropertyFilter;
  using ItemRecord = lexicon::ItemRecord;
  using LinkRecord = lexicon::LinkRecord;
  using UsageValueRecord = lexicon::UsageValueRecord;
  using SortOrder = lexicon::SortOrder;

  Result<void> open(const std::string &utf8Path);

  Result<std::map<std::string, std::string>> loadConfiguration() override;
  Result<void>
  saveConfiguration(const std::map<std::string, std::string> &values) override;
  Result<std::vector<GroupRecord>> loadGroups() override;
  Result<int> defaultGroupId() override;
  Result<void> upsertGroup(const GroupRecord &group) override;
  Result<void> deleteGroup(int groupId) override;
  Result<std::vector<ItemTypeRecord>> loadItemTypes(int groupId) override;
  Result<void> upsertItemType(const ItemTypeRecord &itemType) override;
  Result<int> countItemsForType(int itemTypeId) override;
  Result<void> deleteItemType(int itemTypeId) override;
  Result<std::vector<ItemFieldRecord>> loadItemFields(int itemTypeId) override;
  Result<void> upsertItemField(const ItemFieldRecord &field) override;
  Result<int> countFieldValues(int fieldId) override;
  Result<void> deleteItemField(int fieldId) override;
  Result<std::vector<ItemRecord>> loadItems(
      int groupId, int typeId, const std::vector<ItemValueFilter> &valueFilters,
      const std::string &searchText, const ItemColumnFilters &columnFilters,
      const std::vector<ItemPropertyFilter> &propertyFilters,
      const std::string &tagFilter, const std::string &flagFilter,
      int understandingFilter, int statusFilter, int pinnedFilter, int limit,
      int offset, int sortColumn, SortOrder sortOrder) override;
  Result<int> countItems(int groupId, int typeId,
                         const std::vector<ItemValueFilter> &valueFilters,
                         const std::string &searchText,
                         const ItemColumnFilters &columnFilters,
                         const std::vector<ItemPropertyFilter> &propertyFilters,
                         const std::string &tagFilter,
                         const std::string &flagFilter, int understandingFilter,
                         int statusFilter, int pinnedFilter) override;
  Result<ItemRecord> loadItem(int itemId) override;
  Result<void> saveItem(const ItemRecord &item) override;
  Result<int> saveItemReturningId(const ItemRecord &item) override;
  Result<void> deleteItem(int itemId) override;
  Result<std::vector<LinkRecord>> loadLinks(int itemId) override;
  Result<std::vector<LinkRecord>> loadBacklinks(int itemId) override;
  Result<void> saveLink(const LinkRecord &link) override;
  Result<void> deleteLink(int linkId) override;
  Result<void> logItemRead(int itemId) override;
  Result<std::vector<std::string>> loadSuggestions() override;
  Result<std::vector<std::string>> loadItemTitles() override;
  Result<std::vector<UsageValueRecord>> loadTagUsage() override;
  Result<std::vector<UsageValueRecord>> loadFlagUsage() override;
  Result<std::vector<UsageValueRecord>> loadAliasUsage() override;
  Result<int> findItemId(const std::string &title,
                         const std::string &disambiguation) override;
  Result<void> beginUnitOfWork() override;
  Result<void> commitUnitOfWork() override;
  Result<void> rollbackUnitOfWork() override;
  Result<std::string> importBlob(const std::string &sourcePath) override;
  Result<std::string> importBlobData(const std::string &data) override;
  Result<std::string> readBlobData(const std::string &hash) override;
  Result<void> exportBlob(const std::string &hash,
                          const std::string &destinationPath) override;
  Result<lexicon::BlobMaintenanceReport> scanBlobStorage(lexicon::BlobScanDepth depth) override;
  Result<lexicon::BlobGarbageCollectionResult> collectUnusedBlobs(
      const lexicon::BlobMaintenanceReport &scan) override;
  Result<lexicon::BlobIssue> verifyBlob(const std::string &hash) override;
};
