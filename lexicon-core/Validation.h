#pragma once
#include "Records.h"

namespace lexicon {
bool validateGroup(const GroupRecord& group, QString* error = nullptr);
bool validateType(const ItemTypeRecord& type, QString* error = nullptr);
bool validateField(const ItemFieldRecord& field, QString* error = nullptr);
bool validateItem(const ItemRecord& item, const QList<ItemFieldRecord>& fields, QString* error = nullptr);
bool validateLink(const LinkRecord& link, QString* error = nullptr);
bool validFieldValue(const ItemFieldRecord& field, const QString& value);
QStringList cleanedUniqueValues(const QStringList& values);
}
