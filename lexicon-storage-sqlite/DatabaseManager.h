#pragma once

#include "Records.h"
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QList>
#include <QMap>

class DatabaseManager {
public:
    static bool initialize(const QString& dbPath, QString* errorMessage = nullptr);
    static QSqlDatabase database();
    static QMap<QString, QString> loadConfiguration(QString* errorMessage = nullptr);
    static bool saveConfiguration(const QMap<QString, QString>& values, QString* errorMessage = nullptr);

    static QList<GroupRecord> loadGroups(QString* errorMessage = nullptr);
    static int defaultGroupId(QString* errorMessage = nullptr);
    static bool upsertGroup(const GroupRecord& group, QString* errorMessage = nullptr);
    static bool deleteGroup(int groupId, QString* errorMessage = nullptr);

    static QList<ItemTypeRecord> loadItemTypes(int groupId = -1, QString* errorMessage = nullptr);
    static bool upsertItemType(const ItemTypeRecord& itemType, QString* errorMessage = nullptr);
    static int countItemsForType(int itemTypeId, QString* errorMessage = nullptr);
    static bool deleteItemType(int itemTypeId, QString* errorMessage = nullptr);

    static QList<ItemFieldRecord> loadItemFields(int itemTypeId, QString* errorMessage = nullptr);
    static bool upsertItemField(const ItemFieldRecord& field, QString* errorMessage = nullptr);
    static int countFieldValues(int fieldId, QString* errorMessage = nullptr);
    static bool deleteItemField(int fieldId, QString* errorMessage = nullptr);

    static QList<ItemRecord> loadItems(int groupId, int typeId, const QList<ItemValueFilter>& valueFilters, const QString& searchText, const ItemColumnFilters& columnFilters, const QList<ItemPropertyFilter>& propertyFilters, const QString& tagFilter = QString(), const QString& flagFilter = QString(), int understandingFilter = -1, int statusFilter = -1, int pinnedFilter = -1, int limit = -1, int offset = 0, int sortColumn = 3, SortOrder sortOrder = SortOrder::Ascending, QString* errorMessage = nullptr);
    static int countItems(int groupId, int typeId, const QList<ItemValueFilter>& valueFilters, const QString& searchText, const ItemColumnFilters& columnFilters, const QList<ItemPropertyFilter>& propertyFilters, const QString& tagFilter = QString(), const QString& flagFilter = QString(), int understandingFilter = -1, int statusFilter = -1, int pinnedFilter = -1, QString* errorMessage = nullptr);
    static bool loadItem(int itemId, ItemRecord& outItem, QString* errorMessage = nullptr);
    static bool saveItem(const ItemRecord& item, QString* errorMessage = nullptr, int* savedId = nullptr);
    static bool deleteItem(int itemId, QString* errorMessage = nullptr);

    static QList<LinkRecord> loadLinks(int itemId, QString* errorMessage = nullptr);
    static QList<LinkRecord> loadBacklinks(int itemId, QString* errorMessage = nullptr);
    static bool saveLink(const LinkRecord& link, QString* errorMessage = nullptr);
    static bool deleteLink(int linkId, QString* errorMessage = nullptr);
    static bool logItemRead(int itemId, QString* errorMessage = nullptr);

    static QStringList loadSuggestions(QString* errorMessage = nullptr);
    static QStringList loadItemTitles(QString* errorMessage = nullptr);
    static QList<UsageValueRecord> loadTagUsage(QString* errorMessage = nullptr);
    static QList<UsageValueRecord> loadFlagUsage(QString* errorMessage = nullptr);
    static QList<UsageValueRecord> loadAliasUsage(QString* errorMessage = nullptr);

private:
    static bool logOperation(const QString& tableName, int recordId, int logType, QString* errorMessage = nullptr); // 1=created, 2=updated, 3=deleted, 4=read
    static bool applyMigrations(QString* errorMessage = nullptr);
    static bool execStatements(const QStringList& statements, QString* errorMessage);
    static bool replaceStringValues(const QString& tableName, int itemId, const QStringList& values, QString* errorMessage);
    static QList<UsageValueRecord> loadUsageTable(const QString& sql, QString* errorMessage);
};
