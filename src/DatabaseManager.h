#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QList>
#include <QMap>

enum class ItemStatus {
    None = 0,
    Draft = 1,
    Completed = 2
};

enum class UnderstandingLevel {
    Unknown = 0,      // Never encountered
    Recognized = 1,   // Seen before, can identify
    Understood = 2,   // Conceptually grasped
    Practiced = 3,    // Can apply in real situations
    Mastered = 4      // Fully internalized, can teach or innovate
};

enum class LinkType {
    None = 0,
    IsA = 1,
    PartOf = 2,
    Uses = 3,
    DependsOn = 4,
    Implements = 5,
    Related = 6,
    Contrasts = 7,
    AlternativeTo = 8,
    ParentOf = 9,
    Custom = 10
};

enum class FieldDataType {
    Integer = 0,
    Float = 1,
    Text = 2,
    Date = 3,
    Time = 4,
    Timestamp = 5,
    Boolean = 6,
    Enum = 7,
    Blob = 8,
    Other = 9
};

struct GroupRecord {
    int id = -1;
    QString name;
    QString description;
    int position = 0;
};

struct ItemTypeRecord {
    int id = -1;
    int groupId = -1; // -1 means available in every group
    QString groupName;
    QString name;
    QString description;
};

struct ItemFieldRecord {
    int id = -1;
    int itemTypeId = -1;
    QString name;
    FieldDataType dataType = FieldDataType::Text;
    int position = 0;
    QStringList enumOptions;
};

struct ItemValueFilter {
    int fieldId = -1;
    QString value;
    bool exact = false;
};

struct PropertyRecord {
    QString key;
    QString value;
};

struct LinkRecord {
    int id = -1;
    int fromItemId = -1;
    int toItemId = -1;
    LinkType linkType = LinkType::None;
    int position = 0;
    QString customValue;

    // Optional for UI:
    QString fromItemTitle;
    QString toItemTitle;
};

struct ItemRecord {
    int id = -1;
    int groupId = -1;
    QString groupName;
    int itemTypeId = -1; // -1 means no type
    QString itemTypeName;
    QMap<int, QString> fieldValues;
    QList<PropertyRecord> properties;
    QString title;
    QString disambiguation;
    QStringList aliases;
    QStringList tags;
    QStringList flags;
    ItemStatus status = ItemStatus::None;
    UnderstandingLevel understanding = UnderstandingLevel::Unknown;
    bool pinned = false;
    QString content;
};

struct UsageValueRecord {
    QString value;
    int usageCount = 0;
};

class DatabaseManager {
public:
    static bool initialize(const QString& dbPath, QString* errorMessage = nullptr);
    static QSqlDatabase database();

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

    static QList<ItemRecord> loadItems(int groupId, int typeId, const QList<ItemValueFilter>& valueFilters, const QString& searchText, const QString& tagFilter = QString(), const QString& flagFilter = QString(), int understandingFilter = -1, int statusFilter = -1, int pinnedFilter = -1, int limit = -1, int offset = 0, int sortColumn = 3, Qt::SortOrder sortOrder = Qt::AscendingOrder, QString* errorMessage = nullptr);
    static int countItems(int groupId, int typeId, const QList<ItemValueFilter>& valueFilters, const QString& searchText, const QString& tagFilter = QString(), const QString& flagFilter = QString(), int understandingFilter = -1, int statusFilter = -1, int pinnedFilter = -1, QString* errorMessage = nullptr);
    static bool loadItem(int itemId, ItemRecord& outItem, QString* errorMessage = nullptr);
    static bool saveItem(const ItemRecord& item, QString* errorMessage = nullptr);
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
