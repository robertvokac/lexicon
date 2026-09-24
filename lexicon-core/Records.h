#pragma once

#include <cstdint>
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
  Other = 9,
  // A stored image file: "<media type>:<SHA-256>" (see ImageValue.h).
  Image = 10
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
  // When the item was last reviewed and when it is due again, as UTC
  // "YYYY-MM-DDTHH:MM:SSZ"; both empty for an item never reviewed, which is
  // due now. Saving an item keeps them; a review sets them.
  std::string reviewedAt;
  std::string reviewDueAt;
  // Why a search found this item: the piece of its content around the match,
  // with "…" where it was cut. Empty unless a search text was given that the
  // content matched - an item found by its title or a tag alone needs no
  // explanation. Never stored; it belongs to one search, not to the item.
  std::string matchSnippet;
};
// A reminder at a moment: when it goes off, as UTC "YYYY-MM-DDTHH:MM:SSZ".
struct AlarmRecord {
  int id = -1;
  std::string title;
  std::string description;
  std::string firesAt;
  // When someone dismissed it after it went off, as UTC; empty while it has
  // not gone off or is still ringing.
  std::string dismissedAt;
};
// A question about an item and its answer, for active recall. It belongs to
// exactly one item and goes when the item goes. The counts and the last
// attempt are kept by the system: a quiz answer moves them, an edit does not.
struct CardRecord {
  int id = -1;
  ItemId itemId = -1;
  // Plain UTF-8 text, possibly several lines.
  std::string question;
  std::string answer;
  // How often the person knew the answer, and how often not.
  std::int64_t successCount = 0;
  std::int64_t failureCount = 0;
  // When it was last answered, as UTC "YYYY-MM-DDTHH:MM:SSZ"; empty for a
  // card never attempted.
  std::string lastAttempt;
};
struct UsageValueRecord {
  std::string value;
  int usageCount = 0;
};
} // namespace lexicon
