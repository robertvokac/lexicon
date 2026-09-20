#include "DatabaseManager.h"
#include "Validation.h"
#include "Conversions.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QTime>

#include <cmath>
#include <algorithm>

namespace {
constexpr const char* kConnectionName = "lexicon_connection";

QString normalizeNullable(const QString& value) {
    const QString trimmed = value.trimmed();
    return trimmed;
}

QStringList cleanedUniqueValues(const QStringList& values) {
    QSet<QString> seen;
    QStringList result;
    for (const auto& raw : values) {
        const QString value = raw.trimmed();
        if (value.isEmpty() || seen.contains(value.toCaseFolded())) continue;
        seen.insert(value.toCaseFolded());
        result.push_back(value);
    }
    std::sort(result.begin(), result.end(), [](const QString& a, const QString& b) {
        return a.localeAwareCompare(b) < 0;
    });
    return result;
}

bool setError(QString* errorMessage, const QString& message) {
    if (errorMessage) {
        *errorMessage = message;
    }
    return false;
}

bool beginWrite(QSqlDatabase& db) { QSqlQuery query(db); return query.exec("SAVEPOINT lexicon_write"); }
bool commitWrite(QSqlDatabase& db) { QSqlQuery query(db); return query.exec("RELEASE SAVEPOINT lexicon_write"); }
void rollbackWrite(QSqlDatabase& db) {
    QSqlQuery query(db);
    query.exec("ROLLBACK TO SAVEPOINT lexicon_write");
    query.exec("RELEASE SAVEPOINT lexicon_write");
}

void appendColumnFilters(QString& sql, const ItemColumnFilters& filters) {
    if (!filters.id.isEmpty()) sql += "AND t.id = ? ";
    if (!filters.title.isEmpty()) sql += "AND INSTR(LOWER(t.title), LOWER(?)) > 0 ";
    if (!filters.disambiguation.isEmpty()) {
        sql += "AND INSTR(LOWER(COALESCE(t.disambiguation, '')), LOWER(?)) > 0 ";
    }
    if (!filters.alias.isEmpty()) {
        sql += "AND EXISTS (SELECT 1 FROM alias a WHERE a.item_id = t.id "
               "AND INSTR(LOWER(a.alias), LOWER(?)) > 0) ";
    }
}

void bindColumnFilters(QSqlQuery& query, const ItemColumnFilters& filters) {
    if (!filters.id.isEmpty()) query.addBindValue(filters.id);
    if (!filters.title.isEmpty()) query.addBindValue(filters.title);
    if (!filters.disambiguation.isEmpty()) query.addBindValue(filters.disambiguation);
    if (!filters.alias.isEmpty()) query.addBindValue(filters.alias);
}

void appendPropertyFilters(QString& sql, const QList<ItemPropertyFilter>& filters) {
    for (const auto& filter : filters) {
        if (filter.key.trimmed().isEmpty()) continue;
        sql += "AND EXISTS (SELECT 1 FROM property p WHERE p.item_id = t.id "
               "AND p.\"key\" = ? COLLATE NOCASE ";
        if (!filter.value.trimmed().isEmpty()) {
            sql += "AND INSTR(LOWER(p.value), LOWER(?)) > 0 ";
        }
        sql += ") ";
    }
}

void bindPropertyFilters(QSqlQuery& query, const QList<ItemPropertyFilter>& filters) {
    for (const auto& filter : filters) {
        if (filter.key.trimmed().isEmpty()) continue;
        query.addBindValue(filter.key.trimmed());
        if (!filter.value.trimmed().isEmpty()) query.addBindValue(filter.value.trimmed());
    }
}
}

bool DatabaseManager::initialize(const QString& dbPath, QString* errorMessage) {
    QSqlDatabase db;
    if (QSqlDatabase::contains(kConnectionName)) {
        db = QSqlDatabase::database(kConnectionName);
    } else {
        db = QSqlDatabase::addDatabase("QSQLITE", kConnectionName);
    }

    db.setDatabaseName(dbPath);
    if (!db.open()) {
        return setError(errorMessage, QString("Cannot open database: %1").arg(db.lastError().text()));
    }

    QSqlQuery pragmaQuery(db);
    if (!pragmaQuery.exec("PRAGMA foreign_keys = ON;")) {
        return setError(errorMessage, QString("Cannot enable foreign keys: %1").arg(pragmaQuery.lastError().text()));
    }

    return applyMigrations(errorMessage);
}

QSqlDatabase DatabaseManager::database() {
    return QSqlDatabase::database(kConnectionName);
}

QMap<QString, QString> DatabaseManager::loadConfiguration(QString* errorMessage) {
    QMap<QString, QString> values;
    QSqlQuery query(database());
    if (!query.exec("SELECT \"key\", value FROM configuration;")) {
        setError(errorMessage, query.lastError().text());
        return {};
    }
    while (query.next()) values.insert(query.value(0).toString(), query.value(1).toString());
    return values;
}

bool DatabaseManager::saveConfiguration(const QMap<QString, QString>& values, QString* errorMessage) {
    if (values.isEmpty()) return true;
    QSqlDatabase db = database();
    if (!db.transaction()) return setError(errorMessage, db.lastError().text());
    QSqlQuery query(db);
    query.prepare("INSERT INTO configuration(\"key\", value) VALUES(?, ?) "
                  "ON CONFLICT(\"key\") DO UPDATE SET value = excluded.value;");
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (it.key().trimmed().isEmpty()) {
            db.rollback();
            return setError(errorMessage, "Configuration key cannot be empty.");
        }
        query.bindValue(0, it.key());
        query.bindValue(1, it.value());
        if (!query.exec()) {
            db.rollback();
            return setError(errorMessage, query.lastError().text());
        }
    }
    if (!db.commit()) {
        db.rollback();
        return setError(errorMessage, db.lastError().text());
    }
    return true;
}

QList<GroupRecord> DatabaseManager::loadGroups(QString* errorMessage) {
    QList<GroupRecord> groups;
    QSqlQuery query(database());
    if (!query.exec("SELECT id, name, description, position FROM item_group ORDER BY position, name COLLATE NOCASE, id;")) {
        setError(errorMessage, query.lastError().text());
        return groups;
    }

    while (query.next()) {
        GroupRecord group;
        group.id = query.value(0).toInt();
        group.name = query.value(1).toString();
        group.description = query.value(2).toString();
        group.position = query.value(3).toInt();
        groups.push_back(group);
    }
    return groups;
}

int DatabaseManager::defaultGroupId(QString* errorMessage) {
    QSqlQuery query(database());
    const QString findDefault = "SELECT id FROM item_group WHERE name = 'Default' LIMIT 1;";
    if (!query.exec(findDefault)) {
        setError(errorMessage, query.lastError().text());
        return -1;
    }
    if (query.next()) {
        return query.value(0).toInt();
    }
    query.finish();

    // A user can delete or rename Default; restore it when a new item needs it.
    QSqlQuery positionQuery(database());
    if (!positionQuery.exec("SELECT COALESCE(MAX(position) + 1, 0) FROM item_group;") || !positionQuery.next()) {
        setError(errorMessage, positionQuery.lastError().text());
        return -1;
    }
    GroupRecord group;
    group.name = "Default";
    group.description = "Default group for new items when no group is selected.";
    group.position = positionQuery.value(0).toInt();
    positionQuery.finish();
    if (!upsertGroup(group, errorMessage)) {
        return -1;
    }
    if (!query.exec(findDefault) || !query.next()) {
        setError(errorMessage, "Failed to find the Default group after creating it.");
        return -1;
    }
    return query.value(0).toInt();
}

bool DatabaseManager::upsertGroup(const GroupRecord& group, QString* errorMessage) {
    QSqlDatabase db = database();
    if (!db.transaction()) {
        return setError(errorMessage, db.lastError().text());
    }

    QSqlQuery query(db);
    const QString trimmedName = group.name.trimmed();
    const QString trimmedDescription = group.description.trimmed();

    int groupId = group.id;
    int logType = 2; // updated

    if (group.id < 0) {
        query.prepare("INSERT INTO item_group(name, description, position) VALUES(?, ?, ?);");
        query.addBindValue(trimmedName);
        query.addBindValue(trimmedDescription);
        query.addBindValue(group.position);
        logType = 1; // created
    } else {
        query.prepare("UPDATE item_group SET name = ?, description = ?, position = ? WHERE id = ?;");
        query.addBindValue(trimmedName);
        query.addBindValue(trimmedDescription);
        query.addBindValue(group.position);
        query.addBindValue(group.id);
    }

    if (!query.exec()) {
        db.rollback();
        return setError(errorMessage, query.lastError().text());
    }

    if (group.id < 0) {
        groupId = query.lastInsertId().toInt();
    }

    if (!logOperation("item_group", groupId, logType, errorMessage)) {
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        db.rollback();
        return setError(errorMessage, db.lastError().text());
    }
    return true;
}

bool DatabaseManager::deleteGroup(int groupId, QString* errorMessage) {
    QSqlDatabase db = database();
    if (!db.transaction()) {
        return setError(errorMessage, db.lastError().text());
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM item_group WHERE id = ?;");
    query.addBindValue(groupId);
    if (!query.exec()) {
        db.rollback();
        return setError(errorMessage, query.lastError().text());
    }

    if (!logOperation("item_group", groupId, 3, errorMessage)) {
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        db.rollback();
        return setError(errorMessage, db.lastError().text());
    }
    return true;
}

QList<ItemTypeRecord> DatabaseManager::loadItemTypes(int groupId, QString* errorMessage) {
    QList<ItemTypeRecord> types;
    QSqlQuery query(database());
    QString sql =
        "SELECT ty.id, ty.group_id, COALESCE(g.name, ''), ty.name, ty.description "
        "FROM item_type ty LEFT JOIN item_group g ON g.id = ty.group_id ";
    if (groupId > 0) {
        sql += "WHERE ty.group_id IS NULL OR ty.group_id = ? ";
    }
    sql += "ORDER BY ty.name COLLATE NOCASE, ty.group_id IS NULL, g.name COLLATE NOCASE, ty.id;";
    query.prepare(sql);
    if (groupId > 0) {
        query.addBindValue(groupId);
    }
    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return types;
    }
    while (query.next()) {
        ItemTypeRecord type;
        type.id = query.value(0).toInt();
        type.groupId = query.value(1).isNull() ? -1 : query.value(1).toInt();
        type.groupName = query.value(2).toString();
        type.name = query.value(3).toString();
        type.description = query.value(4).toString();
        types.push_back(type);
    }
    return types;
}

bool DatabaseManager::upsertItemType(const ItemTypeRecord& itemType, QString* errorMessage) {
    const QString name = itemType.name.trimmed();
    if (name.isEmpty()) {
        return setError(errorMessage, "Type name cannot be empty.");
    }
    QSqlDatabase db = database();
    if (!db.transaction()) {
        return setError(errorMessage, db.lastError().text());
    }
    QSqlQuery query(db);
    if (itemType.id < 0) {
        query.prepare("INSERT INTO item_type(group_id, name, description) VALUES(?, ?, ?);");
    } else {
        query.prepare("UPDATE item_type SET group_id = ?, name = ?, description = ? WHERE id = ?;");
    }
    query.addBindValue(itemType.groupId > 0 ? QVariant(itemType.groupId) : QVariant());
    query.addBindValue(name);
    const QString description = itemType.description.trimmed();
    query.addBindValue(description.isNull() ? QStringLiteral("") : description);
    if (itemType.id >= 0) {
        query.addBindValue(itemType.id);
    }
    if (!query.exec()) {
        db.rollback();
        return setError(errorMessage, query.lastError().text());
    }
    const int typeId = itemType.id < 0 ? query.lastInsertId().toInt() : itemType.id;
    if (!logOperation("item_type", typeId, itemType.id < 0 ? 1 : 2, errorMessage)) {
        db.rollback();
        return false;
    }
    if (!db.commit()) {
        db.rollback();
        return setError(errorMessage, db.lastError().text());
    }
    return true;
}

int DatabaseManager::countItemsForType(int itemTypeId, QString* errorMessage) {
    QSqlQuery query(database());
    query.prepare("SELECT COUNT(*) FROM item WHERE item_type_id = ?;");
    query.addBindValue(itemTypeId);
    if (!query.exec() || !query.next()) {
        setError(errorMessage, query.lastError().text());
        return 0;
    }
    return query.value(0).toInt();
}

bool DatabaseManager::deleteItemType(int itemTypeId, QString* errorMessage) {
    QSqlDatabase db = database();
    if (!db.transaction()) {
        return setError(errorMessage, db.lastError().text());
    }
    QSqlQuery query(db);
    query.prepare("DELETE FROM item_type WHERE id = ?;");
    query.addBindValue(itemTypeId);
    if (!query.exec()) {
        db.rollback();
        return setError(errorMessage, query.lastError().text());
    }
    if (!logOperation("item_type", itemTypeId, 3, errorMessage)) {
        db.rollback();
        return false;
    }
    if (!db.commit()) {
        db.rollback();
        return setError(errorMessage, db.lastError().text());
    }
    return true;
}

QList<ItemFieldRecord> DatabaseManager::loadItemFields(int itemTypeId, QString* errorMessage) {
    QList<ItemFieldRecord> fields;
    QSqlQuery query(database());
    query.prepare("SELECT id, item_type_id, name, data_type, position, enum_options "
                  "FROM item_field WHERE item_type_id = ? ORDER BY position, name COLLATE NOCASE, id;");
    query.addBindValue(itemTypeId);
    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return fields;
    }
    while (query.next()) {
        ItemFieldRecord field;
        field.id = query.value(0).toInt();
        field.itemTypeId = query.value(1).toInt();
        field.name = query.value(2).toString();
        field.dataType = static_cast<FieldDataType>(query.value(3).toInt());
        field.position = query.value(4).toInt();
        const QJsonDocument options = QJsonDocument::fromJson(query.value(5).toString().toUtf8());
        if (!options.isArray()) {
            setError(errorMessage, "Invalid enum options for field: " + field.name);
            return {};
        }
        for (const auto& value : options.array()) {
            if (value.isString()) {
                field.enumOptions.push_back(value.toString());
            }
        }
        fields.push_back(field);
    }
    return fields;
}

bool DatabaseManager::upsertItemField(const ItemFieldRecord& field, QString* errorMessage) {
    const QString name = field.name.trimmed();
    const int dataType = static_cast<int>(field.dataType);
    if (name.isEmpty() || dataType < 0 || dataType > static_cast<int>(FieldDataType::Other)) {
        return setError(errorMessage, "Field name or data type is invalid.");
    }
    QStringList options;
    QSet<QString> seen;
    if (field.dataType == FieldDataType::Enum) {
        for (const QString& raw : field.enumOptions) {
            const QString option = raw.trimmed();
            if (!option.isEmpty() && !seen.contains(option.toCaseFolded())) {
                options.push_back(option);
                seen.insert(option.toCaseFolded());
            }
        }
        if (options.isEmpty()) {
            return setError(errorMessage, "Enum fields need at least one option.");
        }
    }
    const QString optionsJson = QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(options)).toJson(QJsonDocument::Compact));
    QSqlDatabase db = database();
    if (!db.transaction()) {
        return setError(errorMessage, db.lastError().text());
    }
    QSqlQuery query(db);
    if (field.id >= 0) {
        query.prepare("SELECT data_type, enum_options FROM item_field WHERE id = ?;");
        query.addBindValue(field.id);
        if (!query.exec() || !query.next()) {
            db.rollback();
            return setError(errorMessage, "Field not found.");
        }
        const bool valuesInvalidated = query.value(0).toInt() != dataType
            || query.value(1).toString() != optionsJson;
        query.finish();
        if (valuesInvalidated) {
            query.prepare("DELETE FROM item_value WHERE item_field_id = ?;");
            query.addBindValue(field.id);
            if (!query.exec()) {
                db.rollback();
                return setError(errorMessage, query.lastError().text());
            }
        }
        query.prepare("UPDATE item_field SET name = ?, data_type = ?, position = ?, enum_options = ? WHERE id = ?;");
    } else {
        query.prepare("INSERT INTO item_field(item_type_id, name, data_type, position, enum_options) VALUES(?, ?, ?, ?, ?);");
        query.addBindValue(field.itemTypeId);
    }
    query.addBindValue(name);
    query.addBindValue(dataType);
    query.addBindValue(field.position);
    query.addBindValue(optionsJson);
    if (field.id >= 0) {
        query.addBindValue(field.id);
    }
    if (!query.exec()) {
        db.rollback();
        return setError(errorMessage, query.lastError().text());
    }
    const int fieldId = field.id < 0 ? query.lastInsertId().toInt() : field.id;
    if (!logOperation("item_field", fieldId, field.id < 0 ? 1 : 2, errorMessage)) {
        db.rollback();
        return false;
    }
    if (!db.commit()) {
        db.rollback();
        return setError(errorMessage, db.lastError().text());
    }
    return true;
}

int DatabaseManager::countFieldValues(int fieldId, QString* errorMessage) {
    QSqlQuery query(database());
    query.prepare("SELECT COUNT(*) FROM item_value WHERE item_field_id = ?;");
    query.addBindValue(fieldId);
    if (!query.exec() || !query.next()) {
        setError(errorMessage, query.lastError().text());
        return 0;
    }
    return query.value(0).toInt();
}

bool DatabaseManager::deleteItemField(int fieldId, QString* errorMessage) {
    QSqlDatabase db = database();
    if (!db.transaction()) {
        return setError(errorMessage, db.lastError().text());
    }
    QSqlQuery query(db);
    query.prepare("DELETE FROM item_field WHERE id = ?;");
    query.addBindValue(fieldId);
    if (!query.exec()) {
        db.rollback();
        return setError(errorMessage, query.lastError().text());
    }
    if (!logOperation("item_field", fieldId, 3, errorMessage)) {
        db.rollback();
        return false;
    }
    if (!db.commit()) {
        db.rollback();
        return setError(errorMessage, db.lastError().text());
    }
    return true;
}

QList<ItemRecord> DatabaseManager::loadItems(int groupId, int typeId, const QList<ItemValueFilter>& valueFilters, const QString& searchText, const ItemColumnFilters& columnFilters, const QList<ItemPropertyFilter>& propertyFilters, const QString& tagFilter, const QString& flagFilter, int understandingFilter, int statusFilter, int pinnedFilter, int limit, int offset, int sortColumn, SortOrder sortOrder, QString* errorMessage) {
    QList<ItemRecord> items;

    QString sql =
        "SELECT t.id, m.name, t.group_id, t.title, t.disambiguation, "
        "COALESCE((SELECT GROUP_CONCAT(a.alias, ', ') FROM alias a WHERE a.item_id = t.id), '') AS aliases, "
        "COALESCE((SELECT GROUP_CONCAT(g.name, ', ') FROM tag g WHERE g.item_id = t.id), '') AS tags, "
        "COALESCE((SELECT GROUP_CONCAT(f.name, ', ') FROM flag f WHERE f.item_id = t.id), '') AS flags, "
        "t.understanding, t.status, t.pinned, "
        "COALESCE(ty.name || CASE WHEN ty.group_id IS NULL THEN ' (All groups)' ELSE '' END, '') "
        "FROM item t "
        "JOIN item_group m ON m.id = t.group_id "
        "LEFT JOIN item_type ty ON ty.id = t.item_type_id "
        "WHERE 1 = 1 ";

    QString filters;
    if (groupId > 0) {
        filters += "AND t.group_id = ? ";
    }
    if (typeId > 0) {
        filters += "AND t.item_type_id = ? ";
    }
    for (const auto& filter : valueFilters) {
        filters += filter.exact
            ? "AND EXISTS (SELECT 1 FROM item_value iv WHERE iv.item_id = t.id AND iv.item_field_id = ? AND iv.value = ?) "
            : "AND EXISTS (SELECT 1 FROM item_value iv WHERE iv.item_id = t.id AND iv.item_field_id = ? AND INSTR(LOWER(iv.value), LOWER(?)) > 0) ";
    }
    appendColumnFilters(filters, columnFilters);
    appendPropertyFilters(filters, propertyFilters);
    if (understandingFilter >= 0) {
        filters += "AND t.understanding = ? ";
    }
    if (statusFilter >= 0) {
        filters += "AND t.status = ? ";
    }
    if (pinnedFilter >= 0) {
        filters += "AND t.pinned = ? ";
    }
    if (!tagFilter.trimmed().isEmpty()) {
        filters += "AND EXISTS (SELECT 1 FROM tag tg WHERE tg.item_id = t.id AND tg.name = ?) ";
    }
    if (!flagFilter.trimmed().isEmpty()) {
        filters += "AND EXISTS (SELECT 1 FROM flag fg WHERE fg.item_id = t.id AND fg.name = ?) ";
    }
    if (!searchText.trimmed().isEmpty()) {
        filters +=
            "AND (LOWER(t.title) LIKE ? "
            " OR LOWER(COALESCE(t.disambiguation, '')) LIKE ? "
            " OR EXISTS (SELECT 1 FROM alias a WHERE a.item_id = t.id AND LOWER(a.alias) LIKE ?) "
            " OR EXISTS (SELECT 1 FROM tag tg WHERE tg.item_id = t.id AND LOWER(tg.name) LIKE ?) "
            " OR EXISTS (SELECT 1 FROM flag fg WHERE fg.item_id = t.id AND LOWER(fg.name) LIKE ?)) ";
    }

    sql += filters;

    // Table columns: Id, Group, Type, Title, Disambiguation, Tags, Flags, Aliases, Status, Understanding, Pinned
    QString orderClause;
    switch (sortColumn) {
        case 0: orderClause = "t.id"; break;
        case 1: orderClause = "m.position"; break;
        case 2: orderClause = "COALESCE(ty.name, '') COLLATE NOCASE"; break;
        case 3: orderClause = "t.title COLLATE NOCASE"; break;
        case 4: orderClause = "COALESCE(t.disambiguation, '') COLLATE NOCASE"; break;
        case 5: orderClause = "tags COLLATE NOCASE"; break;
        case 6: orderClause = "flags COLLATE NOCASE"; break;
        case 7: orderClause = "aliases COLLATE NOCASE"; break;
        case 8: orderClause = "t.status"; break;
        case 9: orderClause = "t.understanding"; break;
        case 10: orderClause = "t.pinned"; break;
        default: orderClause = "t.title COLLATE NOCASE"; break;
    }
    if (sortColumn >= 11 && typeId > 0) {
        QString fieldError;
        const auto fields = loadItemFields(typeId, &fieldError);
        if (!fieldError.isEmpty()) {
            setError(errorMessage, fieldError);
            return items;
        }
        const int fieldIndex = sortColumn - 11;
        if (fieldIndex < fields.size()) {
            const auto& field = fields.at(fieldIndex);
            const QString valueSql = QString("(SELECT iv.value FROM item_value iv WHERE iv.item_id = t.id AND iv.item_field_id = %1)").arg(field.id);
            orderClause = field.dataType == FieldDataType::Integer || field.dataType == FieldDataType::Float
                ? "CAST(" + valueSql + " AS REAL)" : valueSql + " COLLATE NOCASE";
        }
    }

    sql += "ORDER BY " + orderClause + (sortOrder == SortOrder::Ascending ? " ASC " : " DESC ");
    if (sortColumn == 1) {
        sql += ", m.name COLLATE NOCASE";
    }
    
    // Secondary sort for stability
    if (sortColumn != 3) {
        sql += ", t.title COLLATE NOCASE";
    }
    if (sortColumn != 4) {
        sql += ", COALESCE(t.disambiguation, '') COLLATE NOCASE";
    }
    sql += ", t.id ASC ";

    if (limit > 0) {
        sql += "LIMIT ? OFFSET ? ";
    }
    sql += ";";

    QSqlQuery query(database());
    query.prepare(sql);
    if (groupId > 0) {
        query.addBindValue(groupId);
    }
    if (typeId > 0) {
        query.addBindValue(typeId);
    }
    for (const auto& filter : valueFilters) {
        query.addBindValue(filter.fieldId);
        query.addBindValue(filter.value);
    }
    bindColumnFilters(query, columnFilters);
    bindPropertyFilters(query, propertyFilters);
    if (understandingFilter >= 0) {
        query.addBindValue(understandingFilter);
    }
    if (statusFilter >= 0) {
        query.addBindValue(statusFilter);
    }
    if (pinnedFilter >= 0) {
        query.addBindValue(pinnedFilter);
    }
    if (!tagFilter.trimmed().isEmpty()) {
        query.addBindValue(tagFilter.trimmed());
    }
    if (!flagFilter.trimmed().isEmpty()) {
        query.addBindValue(flagFilter.trimmed());
    }
    if (!searchText.trimmed().isEmpty()) {
        const QString trimmedSearch = searchText.trimmed();
        const QString like = QString("%%%1%").arg(trimmedSearch.toLower());
        for (int i = 0; i < 5; ++i) {
            query.addBindValue(like);
        }
    }
    if (limit > 0) {
        query.addBindValue(limit);
        query.addBindValue(offset);
    }

    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return items;
    }

    while (query.next()) {
        ItemRecord item;
        item.id = query.value(0).toInt();
        item.groupName = query.value(1).toString();
        item.groupId = query.value(2).toInt();
        item.title = query.value(3).toString();
        item.disambiguation = query.value(4).toString();
        item.aliases = query.value(5).toString().split(", ", Qt::SkipEmptyParts);
        item.tags = query.value(6).toString().split(", ", Qt::SkipEmptyParts);
        item.flags = query.value(7).toString().split(", ", Qt::SkipEmptyParts);
        item.understanding = static_cast<UnderstandingLevel>(query.value(8).toInt());
        item.status = static_cast<ItemStatus>(query.value(9).toInt());
        item.pinned = query.value(10).toInt() != 0;
        item.itemTypeName = query.value(11).toString();
        items.push_back(item);
    }

    if (typeId > 0 && !items.isEmpty()) {
        QStringList placeholders;
        QMap<int, int> rowByItemId;
        for (int row = 0; row < items.size(); ++row) {
            placeholders.push_back("?");
            rowByItemId.insert(items.at(row).id, row);
        }
        QSqlQuery valuesQuery(database());
        valuesQuery.prepare("SELECT item_id, item_field_id, value FROM item_value WHERE item_id IN ("
                            + placeholders.join(", ") + ");");
        for (const auto& item : items) {
            valuesQuery.addBindValue(item.id);
        }
        if (!valuesQuery.exec()) {
            setError(errorMessage, valuesQuery.lastError().text());
            return {};
        }
        while (valuesQuery.next()) {
            items[rowByItemId.value(valuesQuery.value(0).toInt())].fieldValues.insert(
                valuesQuery.value(1).toInt(), valuesQuery.value(2).toString());
        }
    }

    return items;
}

int DatabaseManager::countItems(int groupId, int typeId, const QList<ItemValueFilter>& valueFilters, const QString& searchText, const ItemColumnFilters& columnFilters, const QList<ItemPropertyFilter>& propertyFilters, const QString& tagFilter, const QString& flagFilter, int understandingFilter, int statusFilter, int pinnedFilter, QString* errorMessage) {
    QString sql = "SELECT COUNT(*) FROM item t WHERE 1 = 1 ";

    if (groupId > 0) {
        sql += "AND t.group_id = ? ";
    }
    if (typeId > 0) {
        sql += "AND t.item_type_id = ? ";
    }
    for (const auto& filter : valueFilters) {
        sql += filter.exact
            ? "AND EXISTS (SELECT 1 FROM item_value iv WHERE iv.item_id = t.id AND iv.item_field_id = ? AND iv.value = ?) "
            : "AND EXISTS (SELECT 1 FROM item_value iv WHERE iv.item_id = t.id AND iv.item_field_id = ? AND INSTR(LOWER(iv.value), LOWER(?)) > 0) ";
    }
    appendColumnFilters(sql, columnFilters);
    appendPropertyFilters(sql, propertyFilters);
    if (understandingFilter >= 0) {
        sql += "AND t.understanding = ? ";
    }
    if (statusFilter >= 0) {
        sql += "AND t.status = ? ";
    }
    if (pinnedFilter >= 0) {
        sql += "AND t.pinned = ? ";
    }
    if (!tagFilter.trimmed().isEmpty()) {
        sql += "AND EXISTS (SELECT 1 FROM tag tg WHERE tg.item_id = t.id AND tg.name = ?) ";
    }
    if (!flagFilter.trimmed().isEmpty()) {
        sql += "AND EXISTS (SELECT 1 FROM flag fg WHERE fg.item_id = t.id AND fg.name = ?) ";
    }
    if (!searchText.trimmed().isEmpty()) {
        sql +=
            "AND (LOWER(t.title) LIKE ? "
            " OR LOWER(COALESCE(t.disambiguation, '')) LIKE ? "
            " OR EXISTS (SELECT 1 FROM alias a WHERE a.item_id = t.id AND LOWER(a.alias) LIKE ?) "
            " OR EXISTS (SELECT 1 FROM tag tg WHERE tg.item_id = t.id AND LOWER(tg.name) LIKE ?) "
            " OR EXISTS (SELECT 1 FROM flag fg WHERE fg.item_id = t.id AND LOWER(fg.name) LIKE ?)) ";
    }

    QSqlQuery query(database());
    query.prepare(sql);
    if (groupId > 0) {
        query.addBindValue(groupId);
    }
    if (typeId > 0) {
        query.addBindValue(typeId);
    }
    for (const auto& filter : valueFilters) {
        query.addBindValue(filter.fieldId);
        query.addBindValue(filter.value);
    }
    bindColumnFilters(query, columnFilters);
    bindPropertyFilters(query, propertyFilters);
    if (understandingFilter >= 0) {
        query.addBindValue(understandingFilter);
    }
    if (statusFilter >= 0) {
        query.addBindValue(statusFilter);
    }
    if (pinnedFilter >= 0) {
        query.addBindValue(pinnedFilter);
    }
    if (!tagFilter.trimmed().isEmpty()) {
        query.addBindValue(tagFilter.trimmed());
    }
    if (!flagFilter.trimmed().isEmpty()) {
        query.addBindValue(flagFilter.trimmed());
    }
    if (!searchText.trimmed().isEmpty()) {
        const QString trimmedSearch = searchText.trimmed();
        const QString like = QString("%%%1%").arg(trimmedSearch.toLower());
        for (int i = 0; i < 5; ++i) {
            query.addBindValue(like);
        }
    }

    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return 0;
    }

    if (query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

bool DatabaseManager::loadItem(int itemId, ItemRecord& outItem, QString* errorMessage) {
    QSqlQuery query(database());
    query.prepare(
        "SELECT t.id, t.group_id, m.name, t.title, COALESCE(t.disambiguation, ''), t.understanding, t.status, t.pinned, COALESCE(t.content, ''), t.item_type_id, COALESCE(ty.name, '') "
        "FROM item t JOIN item_group m ON m.id = t.group_id "
        "LEFT JOIN item_type ty ON ty.id = t.item_type_id WHERE t.id = ?;");
    query.addBindValue(itemId);

    if (!query.exec()) {
        return setError(errorMessage, query.lastError().text());
    }
    if (!query.next()) {
        return setError(errorMessage, "Item not found.");
    }

    outItem.id = query.value(0).toInt();
    outItem.groupId = query.value(1).toInt();
    outItem.groupName = query.value(2).toString();
    outItem.title = query.value(3).toString();
    outItem.disambiguation = query.value(4).toString();
    outItem.understanding = static_cast<UnderstandingLevel>(query.value(5).toInt());
    outItem.status = static_cast<ItemStatus>(query.value(6).toInt());
    outItem.pinned = query.value(7).toInt() != 0;
    outItem.content = query.value(8).toString();
    outItem.itemTypeId = query.value(9).isNull() ? -1 : query.value(9).toInt();
    outItem.itemTypeName = query.value(10).toString();

    QSqlQuery fieldQuery(database());
    fieldQuery.prepare("SELECT item_field_id, value FROM item_value WHERE item_id = ?;");
    fieldQuery.addBindValue(itemId);
    if (!fieldQuery.exec()) {
        return setError(errorMessage, fieldQuery.lastError().text());
    }
    outItem.fieldValues.clear();
    while (fieldQuery.next()) {
        outItem.fieldValues.insert(fieldQuery.value(0).toInt(), fieldQuery.value(1).toString());
    }

    QSqlQuery propertyQuery(database());
    propertyQuery.prepare("SELECT \"key\", value FROM property WHERE item_id = ? ORDER BY \"key\" COLLATE NOCASE;");
    propertyQuery.addBindValue(itemId);
    if (!propertyQuery.exec()) {
        return setError(errorMessage, propertyQuery.lastError().text());
    }
    outItem.properties.clear();
    while (propertyQuery.next()) {
        outItem.properties.push_back({propertyQuery.value(0).toString(), propertyQuery.value(1).toString()});
    }

    auto loadValues = [&](const QString& sql, QStringList& target) -> bool {
        QSqlQuery childQuery(database());
        childQuery.prepare(sql);
        childQuery.addBindValue(itemId);
        if (!childQuery.exec()) {
            return setError(errorMessage, childQuery.lastError().text());
        }
        target.clear();
        while (childQuery.next()) {
            target.push_back(childQuery.value(0).toString());
        }
        return true;
    };

    return loadValues("SELECT alias FROM alias WHERE item_id = ? ORDER BY alias COLLATE NOCASE;", outItem.aliases)
        && loadValues("SELECT name FROM tag WHERE item_id = ? ORDER BY name COLLATE NOCASE;", outItem.tags)
        && loadValues("SELECT name FROM flag WHERE item_id = ? ORDER BY name COLLATE NOCASE;", outItem.flags);
}

bool DatabaseManager::replaceStringValues(const QString& tableName, int itemId, const QStringList& values, QString* errorMessage) {
    QSqlQuery deleteQuery(database());
    deleteQuery.prepare(QString("DELETE FROM %1 WHERE item_id = ?;").arg(tableName));
    deleteQuery.addBindValue(itemId);
    if (!deleteQuery.exec()) {
        return setError(errorMessage, deleteQuery.lastError().text());
    }

    const QString columnName = tableName == "alias" ? "alias" : "name";
    QSqlQuery insertQuery(database());
    insertQuery.prepare(QString("INSERT INTO %1(item_id, %2) VALUES(?, ?);").arg(tableName, columnName));

    const QStringList cleaned = cleanedUniqueValues(values);
    for (const QString& value : cleaned) {
        insertQuery.addBindValue(itemId);
        insertQuery.addBindValue(value);
        if (!insertQuery.exec()) {
            return setError(errorMessage, insertQuery.lastError().text());
        }
        insertQuery.finish();
    }
    return true;
}

bool DatabaseManager::saveItem(const ItemRecord& item, QString* errorMessage, int* savedId) {
    QSet<QString> propertyKeys;
    for (const auto& property : item.properties) {
        const QString key = property.key.trimmed().toCaseFolded();
        if (key.isEmpty() || propertyKeys.contains(key))
            return setError(errorMessage, "Property keys must be nonempty and unique within an item.");
        propertyKeys.insert(key);
    }
    QString fieldError;
    const auto fields = item.itemTypeId > 0 ? loadItemFields(item.itemTypeId, &fieldError) : QList<ItemFieldRecord>();
    if (!fieldError.isEmpty()) return setError(errorMessage, fieldError);
    if (auto valid = lexicon::validateItem(qtbridge::toCore(item), qtbridge::toCore(fields)); !valid)
        return setError(errorMessage, qtbridge::toQt(valid.error().message));
    QSqlDatabase db = database();
    if (!beginWrite(db)) {
        return setError(errorMessage, db.lastError().text());
    }

    int itemId = item.id;
    int logType = 2; // updated
    QSqlQuery query(db);
    if (item.id < 0) {
        query.prepare("INSERT INTO item(group_id, title, disambiguation, understanding, status, pinned, content, item_type_id) VALUES(?, ?, NULLIF(?, ''), ?, ?, ?, ?, ?);");
        query.addBindValue(item.groupId);
        query.addBindValue(item.title.trimmed());
        query.addBindValue(normalizeNullable(item.disambiguation));
        query.addBindValue(static_cast<int>(item.understanding));
        query.addBindValue(static_cast<int>(item.status));
        query.addBindValue(item.pinned ? 1 : 0);
        query.addBindValue(item.content);
        query.addBindValue(item.itemTypeId > 0 ? QVariant(item.itemTypeId) : QVariant());
        if (!query.exec()) {
            rollbackWrite(db);
            return setError(errorMessage, query.lastError().text());
        }
        itemId = query.lastInsertId().toInt();
        logType = 1; // created
    } else {
        query.prepare("UPDATE item SET group_id = ?, title = ?, disambiguation = NULLIF(?, ''), understanding = ?, status = ?, pinned = ?, content = ?, item_type_id = ? WHERE id = ?;");
        query.addBindValue(item.groupId);
        query.addBindValue(item.title.trimmed());
        query.addBindValue(normalizeNullable(item.disambiguation));
        query.addBindValue(static_cast<int>(item.understanding));
        query.addBindValue(static_cast<int>(item.status));
        query.addBindValue(item.pinned ? 1 : 0);
        query.addBindValue(item.content);
        query.addBindValue(item.itemTypeId > 0 ? QVariant(item.itemTypeId) : QVariant());
        query.addBindValue(item.id);
        if (!query.exec()) {
            rollbackWrite(db);
            return setError(errorMessage, query.lastError().text());
        }
    }

    if (!replaceStringValues("alias", itemId, item.aliases, errorMessage)
        || !replaceStringValues("tag", itemId, item.tags, errorMessage)
        || !replaceStringValues("flag", itemId, item.flags, errorMessage)) {
        rollbackWrite(db);
        return false;
    }

    QSqlQuery fieldQuery(db);
    fieldQuery.prepare("DELETE FROM item_value WHERE item_id = ?;");
    fieldQuery.addBindValue(itemId);
    if (!fieldQuery.exec()) {
        rollbackWrite(db);
        return setError(errorMessage, fieldQuery.lastError().text());
    }
    fieldQuery.prepare("INSERT INTO item_value(item_id, item_field_id, value) VALUES(?, ?, ?);");
    for (auto it = item.fieldValues.cbegin(); it != item.fieldValues.cend(); ++it) {
        fieldQuery.addBindValue(itemId);
        fieldQuery.addBindValue(it.key());
        fieldQuery.addBindValue(it.value());
        if (!fieldQuery.exec()) {
            rollbackWrite(db);
            return setError(errorMessage, fieldQuery.lastError().text());
        }
        fieldQuery.finish();
    }

    QSqlQuery propertyQuery(db);
    propertyQuery.prepare("DELETE FROM property WHERE item_id = ?;");
    propertyQuery.addBindValue(itemId);
    if (!propertyQuery.exec()) {
        rollbackWrite(db);
        return setError(errorMessage, propertyQuery.lastError().text());
    }
    propertyQuery.prepare("INSERT INTO property(item_id, \"key\", value) VALUES(?, ?, ?);");
    for (const auto& property : item.properties) {
        propertyQuery.addBindValue(itemId);
        propertyQuery.addBindValue(property.key.trimmed());
        propertyQuery.addBindValue(property.value);
        if (!propertyQuery.exec()) {
            rollbackWrite(db);
            return setError(errorMessage, propertyQuery.lastError().text());
        }
        propertyQuery.finish();
    }

    if (!logOperation("item", itemId, logType, errorMessage)) {
        rollbackWrite(db);
        return false;
    }

    if (!commitWrite(db)) {
        rollbackWrite(db);
        return setError(errorMessage, db.lastError().text());
    }
    if (savedId) *savedId = itemId;
    return true;
}

bool DatabaseManager::deleteItem(int itemId, QString* errorMessage) {
    QSqlDatabase db = database();
    if (!db.transaction()) {
        return setError(errorMessage, db.lastError().text());
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM item WHERE id = ?;");
    query.addBindValue(itemId);
    if (!query.exec()) {
        db.rollback();
        return setError(errorMessage, query.lastError().text());
    }

    if (!logOperation("item", itemId, 3, errorMessage)) {
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        db.rollback();
        return setError(errorMessage, db.lastError().text());
    }
    return true;
}

bool DatabaseManager::logOperation(const QString& tableName, int recordId, int logType, QString* errorMessage) {
    QSqlQuery query(database());
    query.prepare("INSERT INTO log(table_name, record_id, log_type) VALUES(?, ?, ?);");
    query.addBindValue(tableName);
    query.addBindValue(recordId);
    query.addBindValue(logType);
    if (!query.exec()) {
        return setError(errorMessage, query.lastError().text());
    }
    return true;
}

bool DatabaseManager::logItemRead(int itemId, QString* errorMessage) {
    return logOperation("item", itemId, 4, errorMessage);
}

QList<LinkRecord> DatabaseManager::loadLinks(int itemId, QString* errorMessage) {
    QList<LinkRecord> result;
    QSqlDatabase db = database();
    QSqlQuery query(db);
    query.prepare("SELECT l.id, l.from_item_id, l.to_item_id, l.link_type, l.position, l.custom_value, t.title "
                  "FROM link l "
                  "JOIN item t ON l.to_item_id = t.id "
                  "WHERE l.from_item_id = ? "
                  "ORDER BY l.position, t.title COLLATE NOCASE;");
    query.addBindValue(itemId);

    if (!query.exec()) {
        setError(errorMessage, "Failed to load links: " + query.lastError().text());
        return result;
    }

    while (query.next()) {
        LinkRecord link;
        link.id = query.value(0).toInt();
        link.fromItemId = query.value(1).toInt();
        link.toItemId = query.value(2).toInt();
        link.linkType = static_cast<LinkType>(query.value(3).toInt());
        link.position = query.value(4).toInt();
        link.customValue = query.value(5).toString();
        link.toItemTitle = query.value(6).toString();
        result.push_back(link);
    }
    return result;
}

QList<LinkRecord> DatabaseManager::loadBacklinks(int itemId, QString* errorMessage) {
    QList<LinkRecord> result;
    QSqlDatabase db = database();
    QSqlQuery query(db);
    query.prepare("SELECT l.id, l.from_item_id, l.to_item_id, l.link_type, l.position, l.custom_value, t.title "
                  "FROM link l "
                  "JOIN item t ON l.from_item_id = t.id "
                  "WHERE l.to_item_id = ? "
                  "ORDER BY l.position, t.title COLLATE NOCASE;");
    query.addBindValue(itemId);

    if (!query.exec()) {
        setError(errorMessage, "Failed to load backlinks: " + query.lastError().text());
        return result;
    }

    while (query.next()) {
        LinkRecord link;
        link.id = query.value(0).toInt();
        link.fromItemId = query.value(1).toInt();
        link.toItemId = query.value(2).toInt();
        link.linkType = static_cast<LinkType>(query.value(3).toInt());
        link.position = query.value(4).toInt();
        link.customValue = query.value(5).toString();
        link.fromItemTitle = query.value(6).toString();
        result.push_back(link);
    }
    return result;
}

bool DatabaseManager::saveLink(const LinkRecord& link, QString* errorMessage) {
    QSqlDatabase db = database();
    if (!beginWrite(db)) {
        return setError(errorMessage, "Failed to start transaction: " + db.lastError().text());
    }

    QSqlQuery query(db);
    const QString customValue = link.customValue.trimmed().isNull()
        ? QStringLiteral("") : link.customValue.trimmed();
    int logType = 2; // updated
    if (link.id == -1) {
        query.prepare("INSERT INTO link (from_item_id, to_item_id, link_type, position, custom_value) VALUES (?, ?, ?, ?, ?);");
        query.addBindValue(link.fromItemId);
        query.addBindValue(link.toItemId);
        query.addBindValue(static_cast<int>(link.linkType));
        query.addBindValue(link.position);
        query.addBindValue(customValue);
        logType = 1; // created
    } else {
        query.prepare("UPDATE link SET from_item_id = ?, to_item_id = ?, link_type = ?, position = ?, custom_value = ? WHERE id = ?;");
        query.addBindValue(link.fromItemId);
        query.addBindValue(link.toItemId);
        query.addBindValue(static_cast<int>(link.linkType));
        query.addBindValue(link.position);
        query.addBindValue(customValue);
        query.addBindValue(link.id);
    }

    if (!query.exec()) {
        rollbackWrite(db);
        return setError(errorMessage, "Failed to save link: " + query.lastError().text());
    }

    int recordId = link.id;
    if (recordId == -1) {
        recordId = query.lastInsertId().toInt();
    }

    if (!logOperation("link", recordId, logType, errorMessage)) {
        rollbackWrite(db);
        return false;
    }

    if (!commitWrite(db)) {
        rollbackWrite(db);
        return setError(errorMessage, "Failed to commit transaction: " + db.lastError().text());
    }

    return true;
}

bool DatabaseManager::deleteLink(int linkId, QString* errorMessage) {
    QSqlDatabase db = database();
    if (!beginWrite(db)) {
        return setError(errorMessage, "Failed to start transaction: " + db.lastError().text());
    }

    if (!logOperation("link", linkId, 3, errorMessage)) { // 3 = deleted
        rollbackWrite(db);
        return false;
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM link WHERE id = ?;");
    query.addBindValue(linkId);

    if (!query.exec()) {
        rollbackWrite(db);
        return setError(errorMessage, "Failed to delete link: " + query.lastError().text());
    }

    if (!commitWrite(db)) {
        rollbackWrite(db);
        return setError(errorMessage, "Failed to commit transaction: " + db.lastError().text());
    }

    return true;
}

QStringList DatabaseManager::loadSuggestions(QString* errorMessage) {
    QStringList values;
    QSet<QString> seen;

    QSqlQuery query(database());
    if (!query.exec("SELECT title AS value FROM item UNION SELECT alias AS value FROM alias ORDER BY value COLLATE NOCASE;")) {
        setError(errorMessage, query.lastError().text());
        return values;
    }

    while (query.next()) {
        const QString value = query.value(0).toString().trimmed();
        if (value.isEmpty()) {
            continue;
        }
        const QString key = value.toCaseFolded();
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        values.push_back(value);
    }
    return values;
}

QStringList DatabaseManager::loadItemTitles(QString* errorMessage) {
    QStringList values;
    QSqlQuery query(database());
    if (!query.exec("SELECT title, disambiguation FROM item ORDER BY title COLLATE NOCASE;")) {
        setError(errorMessage, query.lastError().text());
        return values;
    }

    while (query.next()) {
        QString title = query.value(0).toString();
        QString disambiguation = query.value(1).toString();
        if (!disambiguation.isEmpty()) {
            values.push_back(QString("%1 [%2]").arg(title, disambiguation));
        } else {
            values.push_back(title);
        }
    }
    return values;
}

QList<UsageValueRecord> DatabaseManager::loadUsageTable(const QString& sql, QString* errorMessage) {
    QList<UsageValueRecord> values;
    QSqlQuery query(database());
    if (!query.exec(sql)) {
        setError(errorMessage, query.lastError().text());
        return values;
    }

    while (query.next()) {
        UsageValueRecord record;
        record.value = query.value(0).toString();
        record.usageCount = query.value(1).toInt();
        values.push_back(record);
    }
    return values;
}

QList<UsageValueRecord> DatabaseManager::loadTagUsage(QString* errorMessage) {
    return loadUsageTable(
        "SELECT name, COUNT(*) AS usage_count FROM tag GROUP BY name ORDER BY name COLLATE NOCASE;",
        errorMessage);
}

QList<UsageValueRecord> DatabaseManager::loadFlagUsage(QString* errorMessage) {
    return loadUsageTable(
        "SELECT name, COUNT(*) AS usage_count FROM flag GROUP BY name ORDER BY name COLLATE NOCASE;",
        errorMessage);
}

QList<UsageValueRecord> DatabaseManager::loadAliasUsage(QString* errorMessage) {
    return loadUsageTable(
        "SELECT alias, COUNT(*) AS usage_count FROM alias GROUP BY alias ORDER BY alias COLLATE NOCASE;",
        errorMessage);
}
