#include "Validation.h"

#include <QDate>
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <QTime>

#include <algorithm>
#include <cmath>

namespace {
bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}
}

namespace lexicon {
QStringList cleanedUniqueValues(const QStringList& values) {
    QSet<QString> seen;
    QStringList result;
    for (const QString& value : values) {
        const QString trimmed = value.trimmed();
        if (trimmed.isEmpty() || seen.contains(trimmed.toCaseFolded())) continue;
        seen.insert(trimmed.toCaseFolded());
        result.push_back(trimmed);
    }
    std::sort(result.begin(), result.end(), [](const QString& a, const QString& b) {
        return a.localeAwareCompare(b) < 0;
    });
    return result;
}

bool validFieldValue(const ItemFieldRecord& field, const QString& value) {
    bool ok = false;
    switch (field.dataType) {
    case FieldDataType::Integer: value.toLongLong(&ok); return ok;
    case FieldDataType::Float: { const double number = value.toDouble(&ok); return ok && std::isfinite(number); }
    case FieldDataType::Date: return QDate::fromString(value, Qt::ISODate).isValid();
    case FieldDataType::Time: return QTime::fromString(value, Qt::ISODate).isValid();
    case FieldDataType::Timestamp: return QDateTime::fromString(value, Qt::ISODate).isValid();
    case FieldDataType::Boolean: return value == "true" || value == "false";
    case FieldDataType::Enum: return field.enumOptions.contains(value);
    case FieldDataType::Blob: return QRegularExpression("^[0-9a-f]{64}$").match(value).hasMatch();
    case FieldDataType::Text:
    case FieldDataType::Other: return true;
    }
    return false;
}

bool validateGroup(const GroupRecord& group, QString* error) {
    return !group.name.trimmed().isEmpty() || fail(error, "Group name cannot be empty.");
}

bool validateType(const ItemTypeRecord& type, QString* error) {
    return !type.name.trimmed().isEmpty() || fail(error, "Type name cannot be empty.");
}

bool validateField(const ItemFieldRecord& field, QString* error) {
    const int kind = static_cast<int>(field.dataType);
    if (field.name.trimmed().isEmpty() || kind < 0 || kind > static_cast<int>(FieldDataType::Other))
        return fail(error, "Field name or data type is invalid.");
    if (field.dataType == FieldDataType::Enum && cleanedUniqueValues(field.enumOptions).isEmpty())
        return fail(error, "Enum fields need at least one option.");
    return true;
}

bool validateItem(const ItemRecord& item, const QList<ItemFieldRecord>& fields, QString* error) {
    if (item.title.trimmed().isEmpty()) return fail(error, "Item title cannot be empty.");
    if (item.groupId <= 0) return fail(error, "Item group is required.");
    QSet<QString> propertyKeys;
    for (const auto& property : item.properties) {
        const QString key = property.key.trimmed().toCaseFolded();
        if (key.isEmpty() || propertyKeys.contains(key))
            return fail(error, "Property keys must be nonempty and unique within an item.");
        propertyKeys.insert(key);
    }
    if (item.itemTypeId <= 0 && !item.fieldValues.isEmpty())
        return fail(error, "An item without a type cannot have field values.");
    QMap<int, ItemFieldRecord> available;
    for (const auto& field : fields) available.insert(field.id, field);
    for (auto it = item.fieldValues.cbegin(); it != item.fieldValues.cend(); ++it) {
        if (!available.contains(it.key()) || !validFieldValue(available.value(it.key()), it.value()))
            return fail(error, "Invalid value for item field " + QString::number(it.key()) + ".");
    }
    return true;
}

bool validateLink(const LinkRecord& link, QString* error) {
    if (link.fromItemId <= 0 || link.toItemId <= 0) return fail(error, "Both link endpoints are required.");
    return true;
}
}
