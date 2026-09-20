#pragma once
#include "Records.h"

class Repository {
public:
    virtual ~Repository() = default;
    virtual QMap<QString, QString> loadConfiguration(QString* errorMessage) = 0;
    virtual bool saveConfiguration(const QMap<QString, QString>& values, QString* errorMessage) = 0;
    virtual QList<GroupRecord> loadGroups(QString* errorMessage) = 0;
    virtual int defaultGroupId(QString* errorMessage) = 0;
    virtual bool upsertGroup(const GroupRecord& group, QString* errorMessage) = 0;
    virtual bool deleteGroup(int groupId, QString* errorMessage) = 0;
    virtual QList<ItemTypeRecord> loadItemTypes(int groupId, QString* errorMessage) = 0;
    virtual bool upsertItemType(const ItemTypeRecord& itemType, QString* errorMessage) = 0;
    virtual int countItemsForType(int itemTypeId, QString* errorMessage) = 0;
    virtual bool deleteItemType(int itemTypeId, QString* errorMessage) = 0;
    virtual QList<ItemFieldRecord> loadItemFields(int itemTypeId, QString* errorMessage) = 0;
    virtual bool upsertItemField(const ItemFieldRecord& field, QString* errorMessage) = 0;
    virtual int countFieldValues(int fieldId, QString* errorMessage) = 0;
    virtual bool deleteItemField(int fieldId, QString* errorMessage) = 0;
    virtual QList<ItemRecord> loadItems(int groupId, int typeId, const QList<ItemValueFilter>& valueFilters, const QString& searchText, const ItemColumnFilters& columnFilters, const QList<ItemPropertyFilter>& propertyFilters, const QString& tagFilter, const QString& flagFilter, int understandingFilter, int statusFilter, int pinnedFilter, int limit, int offset, int sortColumn, SortOrder sortOrder, QString* errorMessage) = 0;
    virtual int countItems(int groupId, int typeId, const QList<ItemValueFilter>& valueFilters, const QString& searchText, const ItemColumnFilters& columnFilters, const QList<ItemPropertyFilter>& propertyFilters, const QString& tagFilter, const QString& flagFilter, int understandingFilter, int statusFilter, int pinnedFilter, QString* errorMessage) = 0;
    virtual bool loadItem(int itemId, ItemRecord& outItem, QString* errorMessage) = 0;
    virtual bool saveItem(const ItemRecord& item, QString* errorMessage) = 0;
    virtual bool deleteItem(int itemId, QString* errorMessage) = 0;
    virtual QList<LinkRecord> loadLinks(int itemId, QString* errorMessage) = 0;
    virtual QList<LinkRecord> loadBacklinks(int itemId, QString* errorMessage) = 0;
    virtual bool saveLink(const LinkRecord& link, QString* errorMessage) = 0;
    virtual bool deleteLink(int linkId, QString* errorMessage) = 0;
    virtual bool logItemRead(int itemId, QString* errorMessage) = 0;
    virtual QStringList loadSuggestions(QString* errorMessage) = 0;
    virtual QStringList loadItemTitles(QString* errorMessage) = 0;
    virtual QList<UsageValueRecord> loadTagUsage(QString* errorMessage) = 0;
    virtual QList<UsageValueRecord> loadFlagUsage(QString* errorMessage) = 0;
    virtual QList<UsageValueRecord> loadAliasUsage(QString* errorMessage) = 0;
    virtual int findItemId(const QString& title, const QString& disambiguation, QString* errorMessage) = 0;
    virtual bool saveItemReturningId(const ItemRecord& item, int* savedId, QString* errorMessage) = 0;
    virtual bool beginUnitOfWork(QString* errorMessage) = 0;
    virtual bool commitUnitOfWork(QString* errorMessage) = 0;
    virtual void rollbackUnitOfWork() = 0;
    virtual QString importBlob(const QString& sourcePath, QString* errorMessage) = 0;
    virtual bool exportBlob(const QString& hash, const QString& destinationPath, QString* errorMessage) = 0;
};
