#pragma once
#include "Repository.h"
#include "DatabaseManager.h"
#include "BlobStore.h"

class SqliteRepository final : public Repository {
public:
    bool open(const QString& path, QString* errorMessage = nullptr) { return DatabaseManager::initialize(path, errorMessage); }
    QMap<QString, QString> loadConfiguration(QString* errorMessage) override { return DatabaseManager::loadConfiguration(errorMessage); }
    bool saveConfiguration(const QMap<QString, QString>& values, QString* errorMessage) override { return DatabaseManager::saveConfiguration(values, errorMessage); }
    QList<GroupRecord> loadGroups(QString* errorMessage) override { return DatabaseManager::loadGroups(errorMessage); }
    int defaultGroupId(QString* errorMessage) override { return DatabaseManager::defaultGroupId(errorMessage); }
    bool upsertGroup(const GroupRecord& group, QString* errorMessage) override { return DatabaseManager::upsertGroup(group, errorMessage); }
    bool deleteGroup(int groupId, QString* errorMessage) override { return DatabaseManager::deleteGroup(groupId, errorMessage); }
    QList<ItemTypeRecord> loadItemTypes(int groupId, QString* errorMessage) override { return DatabaseManager::loadItemTypes(groupId, errorMessage); }
    bool upsertItemType(const ItemTypeRecord& itemType, QString* errorMessage) override { return DatabaseManager::upsertItemType(itemType, errorMessage); }
    int countItemsForType(int itemTypeId, QString* errorMessage) override { return DatabaseManager::countItemsForType(itemTypeId, errorMessage); }
    bool deleteItemType(int itemTypeId, QString* errorMessage) override { return DatabaseManager::deleteItemType(itemTypeId, errorMessage); }
    QList<ItemFieldRecord> loadItemFields(int itemTypeId, QString* errorMessage) override { return DatabaseManager::loadItemFields(itemTypeId, errorMessage); }
    bool upsertItemField(const ItemFieldRecord& field, QString* errorMessage) override { return DatabaseManager::upsertItemField(field, errorMessage); }
    int countFieldValues(int fieldId, QString* errorMessage) override { return DatabaseManager::countFieldValues(fieldId, errorMessage); }
    bool deleteItemField(int fieldId, QString* errorMessage) override { return DatabaseManager::deleteItemField(fieldId, errorMessage); }
    QList<ItemRecord> loadItems(int groupId, int typeId, const QList<ItemValueFilter>& valueFilters, const QString& searchText, const ItemColumnFilters& columnFilters, const QList<ItemPropertyFilter>& propertyFilters, const QString& tagFilter, const QString& flagFilter, int understandingFilter, int statusFilter, int pinnedFilter, int limit, int offset, int sortColumn, SortOrder sortOrder, QString* errorMessage) override { return DatabaseManager::loadItems(groupId, typeId, valueFilters, searchText, columnFilters, propertyFilters, tagFilter, flagFilter, understandingFilter, statusFilter, pinnedFilter, limit, offset, sortColumn, sortOrder, errorMessage); }
    int countItems(int groupId, int typeId, const QList<ItemValueFilter>& valueFilters, const QString& searchText, const ItemColumnFilters& columnFilters, const QList<ItemPropertyFilter>& propertyFilters, const QString& tagFilter, const QString& flagFilter, int understandingFilter, int statusFilter, int pinnedFilter, QString* errorMessage) override { return DatabaseManager::countItems(groupId, typeId, valueFilters, searchText, columnFilters, propertyFilters, tagFilter, flagFilter, understandingFilter, statusFilter, pinnedFilter, errorMessage); }
    bool loadItem(int itemId, ItemRecord& outItem, QString* errorMessage) override { return DatabaseManager::loadItem(itemId, outItem, errorMessage); }
    bool saveItem(const ItemRecord& item, QString* errorMessage) override { return DatabaseManager::saveItem(item, errorMessage); }
    bool deleteItem(int itemId, QString* errorMessage) override { return DatabaseManager::deleteItem(itemId, errorMessage); }
    QList<LinkRecord> loadLinks(int itemId, QString* errorMessage) override { return DatabaseManager::loadLinks(itemId, errorMessage); }
    QList<LinkRecord> loadBacklinks(int itemId, QString* errorMessage) override { return DatabaseManager::loadBacklinks(itemId, errorMessage); }
    bool saveLink(const LinkRecord& link, QString* errorMessage) override { return DatabaseManager::saveLink(link, errorMessage); }
    bool deleteLink(int linkId, QString* errorMessage) override { return DatabaseManager::deleteLink(linkId, errorMessage); }
    bool logItemRead(int itemId, QString* errorMessage) override { return DatabaseManager::logItemRead(itemId, errorMessage); }
    QStringList loadSuggestions(QString* errorMessage) override { return DatabaseManager::loadSuggestions(errorMessage); }
    QStringList loadItemTitles(QString* errorMessage) override { return DatabaseManager::loadItemTitles(errorMessage); }
    QList<UsageValueRecord> loadTagUsage(QString* errorMessage) override { return DatabaseManager::loadTagUsage(errorMessage); }
    QList<UsageValueRecord> loadFlagUsage(QString* errorMessage) override { return DatabaseManager::loadFlagUsage(errorMessage); }
    QList<UsageValueRecord> loadAliasUsage(QString* errorMessage) override { return DatabaseManager::loadAliasUsage(errorMessage); }
    int findItemId(const QString& title, const QString& disambiguation, QString* errorMessage) override;
    bool saveItemReturningId(const ItemRecord& item, int* savedId, QString* errorMessage) override { return DatabaseManager::saveItem(item, errorMessage, savedId); }
    bool beginUnitOfWork(QString* errorMessage) override;
    bool commitUnitOfWork(QString* errorMessage) override;
    void rollbackUnitOfWork() override;
    QString importBlob(const QString& sourcePath, QString* errorMessage) override { return BlobStore::importFile(sourcePath, errorMessage); }
    bool exportBlob(const QString& hash, const QString& destinationPath, QString* errorMessage) override { return BlobStore::exportFile(hash, destinationPath, errorMessage); }
};
