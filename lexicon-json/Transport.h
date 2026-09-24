#pragma once
// JSON transport boundary. JSON types live here and never enter lexicon-core
// or lexicon-application.
#include "Records.h"
#include "Result.h"
#include "Review.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace lexicon {
struct QuizCard;
struct CardQuizSet;
} // namespace lexicon

namespace lexicon::http {
using Json = nlohmann::json;

// A failure that is safe to send to a client. Storage details never reach it.
struct ApiFailure {
  int status = 500;
  std::string code = "internal";
  std::string message = "Internal server error.";
};

// Thrown by the conversion helpers below and translated into HTTP 400.
struct BadRequest {
  std::string message;
};

[[noreturn]] void badRequest(const std::string &message);

// Maps a domain error onto a transport failure. Storage messages are replaced
// with a generic text because they may quote SQL or file system paths.
ApiFailure toApiFailure(const Error &error);
Json errorBody(const ApiFailure &failure);

// Stable symbolic names for the public API. Ordinals are never exposed.
std::string name(ItemStatus value);
std::string name(UnderstandingLevel value);
std::string name(LinkType value);
std::string name(FieldDataType value);
std::string name(SortOrder value);
std::optional<ItemStatus> itemStatusFromName(std::string_view name);
std::optional<UnderstandingLevel> understandingFromName(std::string_view name);
std::optional<LinkType> linkTypeFromName(std::string_view name);
std::optional<FieldDataType> fieldDataTypeFromName(std::string_view name);
std::optional<SortOrder> sortOrderFromName(std::string_view name);
std::optional<ReviewRating> reviewRatingFromName(std::string_view name);

// Records to JSON.
Json toJson(const GroupRecord &group);
Json toJson(const ItemTypeRecord &type);
Json toJson(const ItemFieldRecord &field);
Json toJson(const PropertyRecord &property);
Json toJson(const LinkRecord &link);
Json toJson(const ItemRecord &item);
Json toJson(const UsageValueRecord &usage);
Json toJson(const AlarmRecord &alarm);
Json toJson(const CardRecord &card);
// A card with the title of the item it asks about.
Json toJson(const QuizCard &card);
Json toJson(const CardQuizSet &quiz);
template <class T> Json toJsonArray(const std::vector<T> &values) {
  Json array = Json::array();
  for (const auto &value : values)
    array.push_back(toJson(value));
  return array;
}

// JSON to records. Every helper rejects wrong types and unknown enum names.
GroupRecord groupFromJson(const Json &json);
ItemTypeRecord typeFromJson(const Json &json);
ItemFieldRecord fieldFromJson(const Json &json);
LinkRecord linkFromJson(const Json &json);
AlarmRecord alarmFromJson(const Json &json);
ItemRecord itemFromJson(const Json &json);
std::vector<LinkRecord> linksFromJson(const Json &json);
// What a client sends to create or change a card: its question and answer.
// The statistics are the system's, so a request never sets them.
CardRecord cardFromJson(const Json &json);
// A card as an export carries it: also its item and its statistics, which
// must not be negative.
CardRecord exportedCardFromJson(const Json &json);

// The structured item query body.
struct ItemQuery {
  int groupId = -1;
  int typeId = -1;
  std::vector<ItemValueFilter> valueFilters;
  std::string searchText;
  ItemColumnFilters columnFilters;
  std::vector<ItemPropertyFilter> propertyFilters;
  std::string tagFilter;
  std::string flagFilter;
  int understandingFilter = -1;
  int statusFilter = -1;
  int pinnedFilter = -1;
  int limit = -1;
  int offset = 0;
  int sortColumn = 3;
  SortOrder sortOrder = SortOrder::Ascending;
};
ItemQuery itemQueryFromJson(const Json &json);

// Shared scalar accessors used by the handlers as well.
const Json &requireObject(const Json &json, const char *what);
std::string requiredString(const Json &json, const char *key);
std::string optionalString(const Json &json, const char *key,
                          std::string fallback = {});
int optionalInt(const Json &json, const char *key, int fallback);
bool optionalBool(const Json &json, const char *key, bool fallback);
// An absent value, JSON null and -1 all mean "no ID".
int optionalId(const Json &json, const char *key);
std::vector<std::string> stringArray(const Json &json, const char *key);
// Parses a decimal path segment. Rejects signs, spaces and overflow.
std::optional<int> parseId(std::string_view text);
} // namespace lexicon::http
