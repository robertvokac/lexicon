#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

// Domain data. This library uses QtCore value types but has no Widgets or SQL dependency.
enum class SortOrder { Ascending, Descending };
enum class ItemStatus { None = 0, Draft = 1, Completed = 2 };
enum class UnderstandingLevel { Unknown = 0, Recognized = 1, Understood = 2, Practiced = 3, Mastered = 4 };
enum class LinkType { None = 0, IsA = 1, PartOf = 2, Uses = 3, DependsOn = 4, Implements = 5, Related = 6, Contrasts = 7, AlternativeTo = 8, ParentOf = 9, Custom = 10 };
enum class FieldDataType { Integer = 0, Float = 1, Text = 2, Date = 3, Time = 4, Timestamp = 5, Boolean = 6, Enum = 7, Blob = 8, Other = 9 };

struct GroupRecord { int id = -1; QString name; QString description; int position = 0; };
struct ItemTypeRecord { int id = -1; int groupId = -1; QString groupName; QString name; QString description; };
struct ItemFieldRecord { int id = -1; int itemTypeId = -1; QString name; FieldDataType dataType = FieldDataType::Text; int position = 0; QStringList enumOptions; };
struct ItemValueFilter { int fieldId = -1; QString value; bool exact = false; };
struct ItemColumnFilters { QString id; QString title; QString disambiguation; QString alias; };
struct ItemPropertyFilter { QString key; QString value; };
struct PropertyRecord { QString key; QString value; };
struct LinkRecord {
    int id = -1;
    int fromItemId = -1;
    int toItemId = -1;
    LinkType linkType = LinkType::None;
    int position = 0;
    QString customValue;
    QString fromItemTitle;
    QString toItemTitle;
};
struct ItemRecord {
    int id = -1;
    int groupId = -1;
    QString groupName;
    int itemTypeId = -1;
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
struct UsageValueRecord { QString value; int usageCount = 0; };
