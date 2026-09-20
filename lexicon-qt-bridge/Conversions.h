#pragma once
#include "QtRecords.h"
#include "Records.h"

#include <map>
#include <utility>
#include <vector>

namespace qtbridge {
std::string toCore(const QString &value);
QString toQt(const std::string &value);
std::vector<std::string> toCore(const QStringList &values);
QStringList toQt(const std::vector<std::string> &values);
lexicon::SortOrder toCore(SortOrder value);
SortOrder toQt(lexicon::SortOrder value);
lexicon::GroupRecord toCore(const GroupRecord &value);
GroupRecord toQt(const lexicon::GroupRecord &value);
lexicon::ItemTypeRecord toCore(const ItemTypeRecord &value);
ItemTypeRecord toQt(const lexicon::ItemTypeRecord &value);
lexicon::ItemFieldRecord toCore(const ItemFieldRecord &value);
ItemFieldRecord toQt(const lexicon::ItemFieldRecord &value);
lexicon::ItemValueFilter toCore(const ItemValueFilter &value);
ItemValueFilter toQt(const lexicon::ItemValueFilter &value);
lexicon::ItemColumnFilters toCore(const ItemColumnFilters &value);
ItemColumnFilters toQt(const lexicon::ItemColumnFilters &value);
lexicon::ItemPropertyFilter toCore(const ItemPropertyFilter &value);
ItemPropertyFilter toQt(const lexicon::ItemPropertyFilter &value);
lexicon::PropertyRecord toCore(const PropertyRecord &value);
PropertyRecord toQt(const lexicon::PropertyRecord &value);
lexicon::LinkRecord toCore(const LinkRecord &value);
LinkRecord toQt(const lexicon::LinkRecord &value);
lexicon::ItemRecord toCore(const ItemRecord &value);
ItemRecord toQt(const lexicon::ItemRecord &value);
lexicon::UsageValueRecord toCore(const UsageValueRecord &value);
UsageValueRecord toQt(const lexicon::UsageValueRecord &value);
std::map<int, std::string> toCore(const QMap<int, QString> &values);
QMap<int, QString> toQt(const std::map<int, std::string> &values);
std::map<std::string, std::string> toCore(const QMap<QString, QString> &values);
QMap<QString, QString> toQt(const std::map<std::string, std::string> &values);

inline int toCore(int value) { return value; }
inline int toQt(int value) { return value; }

template <class T>
auto toCore(const QList<T> &values)
    -> std::vector<decltype(toCore(std::declval<T>()))> {
  using Result = decltype(toCore(std::declval<T>()));
  std::vector<Result> result;
  result.reserve(values.size());
  for (const auto &value : values)
    result.push_back(toCore(value));
  return result;
}

template <class T>
auto toQt(const std::vector<T> &values)
    -> QList<decltype(toQt(std::declval<T>()))> {
  using Result = decltype(toQt(std::declval<T>()));
  QList<Result> result;
  result.reserve(static_cast<qsizetype>(values.size()));
  for (const auto &value : values)
    result.push_back(toQt(value));
  return result;
}
} // namespace qtbridge
