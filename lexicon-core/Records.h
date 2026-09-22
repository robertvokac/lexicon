#pragma once

#include <map>
#include <string>
#include <vector>

namespace lexicon {
// All text crossing the core/application boundary is UTF-8.
using ItemId = int;

enum class SortOrder { Ascending, Descending };
enum class ItemStatus { None = 0, Draft = 1, Completed = 2 };
enum class UnderstandingLevel {
  Unknown = 0,
  Recognized = 1,
  Understood = 2,
  Practiced = 3,
  Mastered = 4
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
  std::string name;
  std::string description;
  int position = 0;
};
struct ItemTypeRecord {
  int id = -1;
  int groupId = -1;
  std::string groupName;
  std::string name;
  std::string description;
};
struct ItemFieldRecord {
  int id = -1;
  int itemTypeId = -1;
  std::string name;
  FieldDataType dataType = FieldDataType::Text;
  int position = 0;
  std::vector<std::string> enumOptions;
};
struct ItemValueFilter {
  int fieldId = -1;
  std::string value;
  bool exact = false;
};
struct ItemColumnFilters {
  std::string id;
  std::string title;
  std::string disambiguation;
  std::string alias;
};
struct ItemPropertyFilter {
  std::string key;
  std::string value;
};
struct PropertyRecord {
  std::string key;
  std::string value;
};
struct LinkRecord {
  int id = -1;
  int fromItemId = -1;
  int toItemId = -1;
  LinkType linkType = LinkType::None;
  int position = 0;
  std::string customValue;
  std::string fromItemTitle;
  std::string toItemTitle;
};
struct ItemRecord {
  ItemId id = -1;
  int groupId = -1;
  std::string groupName;
  int itemTypeId = -1;
  std::string itemTypeName;
  std::map<int, std::string> fieldValues;
  std::vector<PropertyRecord> properties;
  std::string title;
  std::string disambiguation;
  std::vector<std::string> aliases;
  std::vector<std::string> tags;
  std::vector<std::string> flags;
  ItemStatus status = ItemStatus::None;
  UnderstandingLevel understanding = UnderstandingLevel::Unknown;
  bool pinned = false;
  std::string content;
  // Counts every change to the item, its values and its links. A save that
  // carries a positive revision is refused when the stored one has moved on;
  // 0 saves unconditionally.
  int revision = 0;
};
struct UsageValueRecord {
  std::string value;
  int usageCount = 0;
};
} // namespace lexicon
