#include "Conversions.h"

namespace qtbridge {
std::string toCore(const QString &value) {
  const QByteArray bytes = value.toUtf8();
  return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}
QString toQt(const std::string &value) {
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
std::vector<std::string> toCore(const QStringList &values) {
  std::vector<std::string> result;
  result.reserve(values.size());
  for (const auto &value : values)
    result.push_back(toCore(value));
  return result;
}
QStringList toQt(const std::vector<std::string> &values) {
  QStringList result;
  for (const auto &value : values)
    result.push_back(toQt(value));
  return result;
}
lexicon::SortOrder toCore(SortOrder value) {
  return static_cast<lexicon::SortOrder>(value);
}
SortOrder toQt(lexicon::SortOrder value) {
  return static_cast<SortOrder>(value);
}
lexicon::GroupRecord toCore(const GroupRecord &v) {
  return {v.id, toCore(v.name), toCore(v.description), v.position};
}
GroupRecord toQt(const lexicon::GroupRecord &v) {
  return {v.id, toQt(v.name), toQt(v.description), v.position};
}
lexicon::ItemTypeRecord toCore(const ItemTypeRecord &v) {
  return {v.id, v.groupId, toCore(v.groupName), toCore(v.name),
          toCore(v.description)};
}
ItemTypeRecord toQt(const lexicon::ItemTypeRecord &v) {
  return {v.id, v.groupId, toQt(v.groupName), toQt(v.name),
          toQt(v.description)};
}
lexicon::ItemFieldRecord toCore(const ItemFieldRecord &v) {
  return {v.id,           v.itemTypeId,
          toCore(v.name), static_cast<lexicon::FieldDataType>(v.dataType),
          v.position,     toCore(v.enumOptions)};
}
ItemFieldRecord toQt(const lexicon::ItemFieldRecord &v) {
  return {v.id,         v.itemTypeId,
          toQt(v.name), static_cast<FieldDataType>(v.dataType),
          v.position,   toQt(v.enumOptions)};
}
lexicon::ItemValueFilter toCore(const ItemValueFilter &v) {
  return {v.fieldId, toCore(v.value), v.exact};
}
ItemValueFilter toQt(const lexicon::ItemValueFilter &v) {
  return {v.fieldId, toQt(v.value), v.exact};
}
lexicon::ItemColumnFilters toCore(const ItemColumnFilters &v) {
  return {toCore(v.id), toCore(v.title), toCore(v.disambiguation),
          toCore(v.alias)};
}
ItemColumnFilters toQt(const lexicon::ItemColumnFilters &v) {
  return {toQt(v.id), toQt(v.title), toQt(v.disambiguation), toQt(v.alias)};
}
lexicon::ItemPropertyFilter toCore(const ItemPropertyFilter &v) {
  return {toCore(v.key), toCore(v.value)};
}
ItemPropertyFilter toQt(const lexicon::ItemPropertyFilter &v) {
  return {toQt(v.key), toQt(v.value)};
}
lexicon::PropertyRecord toCore(const PropertyRecord &v) {
  return {toCore(v.key), toCore(v.value)};
}
PropertyRecord toQt(const lexicon::PropertyRecord &v) {
  return {toQt(v.key), toQt(v.value)};
}
lexicon::LinkRecord toCore(const LinkRecord &v) {
  return {v.id,
          v.fromItemId,
          v.toItemId,
          static_cast<lexicon::LinkType>(v.linkType),
          v.position,
          toCore(v.customValue),
          toCore(v.fromItemTitle),
          toCore(v.toItemTitle)};
}
LinkRecord toQt(const lexicon::LinkRecord &v) {
  return {v.id,
          v.fromItemId,
          v.toItemId,
          static_cast<LinkType>(v.linkType),
          v.position,
          toQt(v.customValue),
          toQt(v.fromItemTitle),
          toQt(v.toItemTitle)};
}
lexicon::ItemRecord toCore(const ItemRecord &v) {
  return {v.id,
          v.groupId,
          toCore(v.groupName),
          v.itemTypeId,
          toCore(v.itemTypeName),
          toCore(v.fieldValues),
          toCore(v.properties),
          toCore(v.title),
          toCore(v.disambiguation),
          toCore(v.aliases),
          toCore(v.tags),
          toCore(v.flags),
          static_cast<lexicon::ItemStatus>(v.status),
          static_cast<lexicon::UnderstandingLevel>(v.understanding),
          v.pinned,
          toCore(v.content)};
}
ItemRecord toQt(const lexicon::ItemRecord &v) {
  return {v.id,
          v.groupId,
          toQt(v.groupName),
          v.itemTypeId,
          toQt(v.itemTypeName),
          toQt(v.fieldValues),
          toQt(v.properties),
          toQt(v.title),
          toQt(v.disambiguation),
          toQt(v.aliases),
          toQt(v.tags),
          toQt(v.flags),
          static_cast<ItemStatus>(v.status),
          static_cast<UnderstandingLevel>(v.understanding),
          v.pinned,
          toQt(v.content)};
}
lexicon::UsageValueRecord toCore(const UsageValueRecord &v) {
  return {toCore(v.value), v.usageCount};
}
UsageValueRecord toQt(const lexicon::UsageValueRecord &v) {
  return {toQt(v.value), v.usageCount};
}
std::map<int, std::string> toCore(const QMap<int, QString> &values) {
  std::map<int, std::string> result;
  for (auto it = values.cbegin(); it != values.cend(); ++it)
    result.emplace(it.key(), toCore(it.value()));
  return result;
}
QMap<int, QString> toQt(const std::map<int, std::string> &values) {
  QMap<int, QString> result;
  for (const auto &[key, value] : values)
    result.insert(key, toQt(value));
  return result;
}
std::map<std::string, std::string>
toCore(const QMap<QString, QString> &values) {
  std::map<std::string, std::string> result;
  for (auto it = values.cbegin(); it != values.cend(); ++it)
    result.emplace(toCore(it.key()), toCore(it.value()));
  return result;
}
QMap<QString, QString> toQt(const std::map<std::string, std::string> &values) {
  QMap<QString, QString> result;
  for (const auto &[key, value] : values)
    result.insert(toQt(key), toQt(value));
  return result;
}
} // namespace qtbridge
