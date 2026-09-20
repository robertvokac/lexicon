#include "Transport.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <utility>

namespace lexicon::http {
namespace {
template <class Enum, std::size_t Count>
std::string lookupName(const std::array<std::pair<Enum, const char *>, Count> &table,
                       Enum value, const char *fallback) {
  for (const auto &[key, text] : table)
    if (key == value)
      return text;
  return fallback;
}
template <class Enum, std::size_t Count>
std::optional<Enum>
lookupValue(const std::array<std::pair<Enum, const char *>, Count> &table,
            std::string_view name) {
  for (const auto &[key, text] : table)
    if (name == text)
      return key;
  return std::nullopt;
}

constexpr std::array<std::pair<ItemStatus, const char *>, 3> kStatusNames{
    {{ItemStatus::None, "None"},
     {ItemStatus::Draft, "Draft"},
     {ItemStatus::Completed, "Completed"}}};
constexpr std::array<std::pair<UnderstandingLevel, const char *>, 5>
    kUnderstandingNames{{{UnderstandingLevel::Unknown, "Unknown"},
                         {UnderstandingLevel::Recognized, "Recognized"},
                         {UnderstandingLevel::Understood, "Understood"},
                         {UnderstandingLevel::Practiced, "Practiced"},
                         {UnderstandingLevel::Mastered, "Mastered"}}};
constexpr std::array<std::pair<LinkType, const char *>, 11> kLinkTypeNames{
    {{LinkType::None, "None"},
     {LinkType::IsA, "IsA"},
     {LinkType::PartOf, "PartOf"},
     {LinkType::Uses, "Uses"},
     {LinkType::DependsOn, "DependsOn"},
     {LinkType::Implements, "Implements"},
     {LinkType::Related, "Related"},
     {LinkType::Contrasts, "Contrasts"},
     {LinkType::AlternativeTo, "AlternativeTo"},
     {LinkType::ParentOf, "ParentOf"},
     {LinkType::Custom, "Custom"}}};
constexpr std::array<std::pair<FieldDataType, const char *>, 10>
    kFieldDataTypeNames{{{FieldDataType::Integer, "Integer"},
                         {FieldDataType::Float, "Float"},
                         {FieldDataType::Text, "Text"},
                         {FieldDataType::Date, "Date"},
                         {FieldDataType::Time, "Time"},
                         {FieldDataType::Timestamp, "Timestamp"},
                         {FieldDataType::Boolean, "Boolean"},
                         {FieldDataType::Enum, "Enum"},
                         {FieldDataType::Blob, "Blob"},
                         {FieldDataType::Other, "Other"}}};
constexpr std::array<std::pair<SortOrder, const char *>, 2> kSortOrderNames{
    {{SortOrder::Ascending, "Ascending"},
     {SortOrder::Descending, "Descending"}}};

const Json *member(const Json &json, const char *key) {
  if (!json.is_object())
    badRequest("Expected a JSON object.");
  const auto found = json.find(key);
  if (found == json.end() || found->is_null())
    return nullptr;
  return &*found;
}

template <class Enum>
Enum requiredEnum(const Json &json, const char *key,
                  std::optional<Enum> (*parse)(std::string_view),
                  const char *what) {
  const auto value = requiredString(json, key);
  if (auto parsed = parse(value))
    return *parsed;
  badRequest(std::string("Unknown ") + what + " '" + value + "'.");
}
} // namespace

void badRequest(const std::string &message) { throw BadRequest{message}; }

ApiFailure toApiFailure(const Error &error) {
  switch (error.code) {
  case Error::Code::Validation:
    return {400, "validation", error.message};
  case Error::Code::NotFound:
    return {404, "not_found", error.message};
  case Error::Code::Storage:
    break;
  }
  // Storage messages may quote SQL or paths, so they stay on the server.
  return {500, "storage", "The server could not complete the operation."};
}

Json errorBody(const ApiFailure &failure) {
  return Json{{"error", {{"code", failure.code}, {"message", failure.message}}}};
}

std::string name(ItemStatus value) {
  return lookupName(kStatusNames, value, "None");
}
std::string name(UnderstandingLevel value) {
  return lookupName(kUnderstandingNames, value, "Unknown");
}
std::string name(LinkType value) {
  return lookupName(kLinkTypeNames, value, "None");
}
std::string name(FieldDataType value) {
  return lookupName(kFieldDataTypeNames, value, "Text");
}
std::string name(SortOrder value) {
  return lookupName(kSortOrderNames, value, "Ascending");
}
std::optional<ItemStatus> itemStatusFromName(std::string_view text) {
  return lookupValue(kStatusNames, text);
}
std::optional<UnderstandingLevel> understandingFromName(std::string_view text) {
  return lookupValue(kUnderstandingNames, text);
}
std::optional<LinkType> linkTypeFromName(std::string_view text) {
  return lookupValue(kLinkTypeNames, text);
}
std::optional<FieldDataType> fieldDataTypeFromName(std::string_view text) {
  return lookupValue(kFieldDataTypeNames, text);
}
std::optional<SortOrder> sortOrderFromName(std::string_view text) {
  return lookupValue(kSortOrderNames, text);
}

const Json &requireObject(const Json &json, const char *what) {
  if (!json.is_object())
    badRequest(std::string(what) + " must be a JSON object.");
  return json;
}

std::string requiredString(const Json &json, const char *key) {
  const Json *value = member(json, key);
  if (!value)
    badRequest(std::string("Field '") + key + "' is required.");
  if (!value->is_string())
    badRequest(std::string("Field '") + key + "' must be a string.");
  return value->get<std::string>();
}

std::string optionalString(const Json &json, const char *key,
                          std::string fallback) {
  const Json *value = member(json, key);
  if (!value)
    return fallback;
  if (!value->is_string())
    badRequest(std::string("Field '") + key + "' must be a string.");
  return value->get<std::string>();
}

int optionalInt(const Json &json, const char *key, int fallback) {
  const Json *value = member(json, key);
  if (!value)
    return fallback;
  if (!value->is_number_integer())
    badRequest(std::string("Field '") + key + "' must be an integer.");
  const auto number = value->get<long long>();
  if (number < std::numeric_limits<int>::min() ||
      number > std::numeric_limits<int>::max())
    badRequest(std::string("Field '") + key + "' is out of range.");
  return static_cast<int>(number);
}

bool optionalBool(const Json &json, const char *key, bool fallback) {
  const Json *value = member(json, key);
  if (!value)
    return fallback;
  if (!value->is_boolean())
    badRequest(std::string("Field '") + key + "' must be a boolean.");
  return value->get<bool>();
}

int optionalId(const Json &json, const char *key) {
  const int id = optionalInt(json, key, -1);
  return id > 0 ? id : -1;
}

std::vector<std::string> stringArray(const Json &json, const char *key) {
  const Json *value = member(json, key);
  std::vector<std::string> values;
  if (!value)
    return values;
  if (!value->is_array())
    badRequest(std::string("Field '") + key + "' must be an array of strings.");
  for (const auto &entry : *value) {
    if (!entry.is_string())
      badRequest(std::string("Field '") + key +
                 "' must contain strings only.");
    values.push_back(entry.get<std::string>());
  }
  return values;
}

std::optional<int> parseId(std::string_view text) {
  if (text.empty() || text.size() > 10 ||
      !std::all_of(text.begin(), text.end(),
                   [](char ch) { return ch >= '0' && ch <= '9'; }))
    return std::nullopt;
  long long value = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() || value <= 0 ||
      value > std::numeric_limits<int>::max())
    return std::nullopt;
  return static_cast<int>(value);
}

Json toJson(const GroupRecord &group) {
  return Json{{"id", group.id > 0 ? Json(group.id) : Json(nullptr)},
              {"name", group.name},
              {"description", group.description},
              {"position", group.position}};
}

Json toJson(const ItemTypeRecord &type) {
  return Json{{"id", type.id > 0 ? Json(type.id) : Json(nullptr)},
              {"groupId", type.groupId > 0 ? Json(type.groupId) : Json(nullptr)},
              {"groupName", type.groupName},
              {"name", type.name},
              {"description", type.description}};
}

Json toJson(const ItemFieldRecord &field) {
  return Json{
      {"id", field.id > 0 ? Json(field.id) : Json(nullptr)},
      {"itemTypeId",
       field.itemTypeId > 0 ? Json(field.itemTypeId) : Json(nullptr)},
      {"name", field.name},
      {"dataType", name(field.dataType)},
      {"position", field.position},
      {"enumOptions", field.enumOptions}};
}

Json toJson(const PropertyRecord &property) {
  return Json{{"key", property.key}, {"value", property.value}};
}

Json toJson(const LinkRecord &link) {
  return Json{{"id", link.id > 0 ? Json(link.id) : Json(nullptr)},
              {"fromItemId",
               link.fromItemId > 0 ? Json(link.fromItemId) : Json(nullptr)},
              {"toItemId",
               link.toItemId > 0 ? Json(link.toItemId) : Json(nullptr)},
              {"linkType", name(link.linkType)},
              {"position", link.position},
              {"customValue", link.customValue},
              {"fromItemTitle", link.fromItemTitle},
              {"toItemTitle", link.toItemTitle}};
}

Json toJson(const ItemRecord &item) {
  Json fieldValues = Json::object();
  for (const auto &[fieldId, value] : item.fieldValues)
    fieldValues[std::to_string(fieldId)] = value;
  return Json{
      {"id", item.id > 0 ? Json(item.id) : Json(nullptr)},
      {"groupId", item.groupId > 0 ? Json(item.groupId) : Json(nullptr)},
      {"groupName", item.groupName},
      {"itemTypeId",
       item.itemTypeId > 0 ? Json(item.itemTypeId) : Json(nullptr)},
      {"itemTypeName", item.itemTypeName},
      {"fieldValues", std::move(fieldValues)},
      {"properties", toJsonArray(item.properties)},
      {"title", item.title},
      {"disambiguation", item.disambiguation},
      {"aliases", item.aliases},
      {"tags", item.tags},
      {"flags", item.flags},
      {"status", name(item.status)},
      {"understanding", name(item.understanding)},
      {"pinned", item.pinned},
      {"content", item.content}};
}

Json toJson(const UsageValueRecord &usage) {
  return Json{{"value", usage.value}, {"usageCount", usage.usageCount}};
}

GroupRecord groupFromJson(const Json &json) {
  requireObject(json, "Group");
  GroupRecord group;
  group.id = optionalId(json, "id");
  group.name = requiredString(json, "name");
  group.description = optionalString(json, "description");
  group.position = optionalInt(json, "position", 0);
  return group;
}

ItemTypeRecord typeFromJson(const Json &json) {
  requireObject(json, "Type");
  ItemTypeRecord type;
  type.id = optionalId(json, "id");
  type.groupId = optionalId(json, "groupId");
  type.name = requiredString(json, "name");
  type.description = optionalString(json, "description");
  return type;
}

ItemFieldRecord fieldFromJson(const Json &json) {
  requireObject(json, "Field");
  ItemFieldRecord field;
  field.id = optionalId(json, "id");
  field.itemTypeId = optionalId(json, "itemTypeId");
  field.name = requiredString(json, "name");
  field.dataType =
      requiredEnum<FieldDataType>(json, "dataType", fieldDataTypeFromName,
                                  "field data type");
  field.position = optionalInt(json, "position", 0);
  field.enumOptions = stringArray(json, "enumOptions");
  return field;
}

LinkRecord linkFromJson(const Json &json) {
  requireObject(json, "Link");
  LinkRecord link;
  link.id = optionalId(json, "id");
  link.fromItemId = optionalId(json, "fromItemId");
  link.toItemId = optionalId(json, "toItemId");
  link.linkType =
      requiredEnum<LinkType>(json, "linkType", linkTypeFromName, "link type");
  link.position = optionalInt(json, "position", 0);
  link.customValue = optionalString(json, "customValue");
  // Titles are read-only projections of the target item.
  return link;
}

std::vector<LinkRecord> linksFromJson(const Json &json) {
  std::vector<LinkRecord> links;
  if (json.is_null())
    return links;
  if (!json.is_array())
    badRequest("Links must be a JSON array.");
  for (const auto &entry : json)
    links.push_back(linkFromJson(entry));
  return links;
}

ItemRecord itemFromJson(const Json &json) {
  requireObject(json, "Item");
  ItemRecord item;
  item.id = optionalId(json, "id");
  item.groupId = optionalId(json, "groupId");
  item.itemTypeId = optionalId(json, "itemTypeId");
  item.title = requiredString(json, "title");
  item.disambiguation = optionalString(json, "disambiguation");
  item.aliases = stringArray(json, "aliases");
  item.tags = stringArray(json, "tags");
  item.flags = stringArray(json, "flags");
  item.content = optionalString(json, "content");
  item.pinned = optionalBool(json, "pinned", false);
  item.status = ItemStatus::None;
  if (member(json, "status") != nullptr)
    item.status = requiredEnum<ItemStatus>(json, "status", itemStatusFromName,
                                           "item status");
  item.understanding = UnderstandingLevel::Unknown;
  if (member(json, "understanding") != nullptr)
    item.understanding = requiredEnum<UnderstandingLevel>(
        json, "understanding", understandingFromName, "understanding level");
  if (const Json *values = member(json, "fieldValues")) {
    if (!values->is_object())
      badRequest("Field 'fieldValues' must be an object keyed by field ID.");
    for (const auto &[key, value] : values->items()) {
      const auto fieldId = parseId(key);
      if (!fieldId)
        badRequest("Field value key '" + key + "' is not a valid field ID.");
      if (!value.is_string())
        badRequest("Field value for field " + key + " must be a string.");
      item.fieldValues[*fieldId] = value.get<std::string>();
    }
  }
  if (const Json *properties = member(json, "properties")) {
    if (!properties->is_array())
      badRequest("Field 'properties' must be an array.");
    for (const auto &entry : *properties) {
      requireObject(entry, "Property");
      item.properties.push_back(
          {requiredString(entry, "key"), optionalString(entry, "value")});
    }
  }
  return item;
}

ItemQuery itemQueryFromJson(const Json &json) {
  requireObject(json, "Query");
  ItemQuery query;
  query.groupId = optionalId(json, "groupId");
  query.typeId = optionalId(json, "typeId");
  query.searchText = optionalString(json, "searchText");
  query.tagFilter = optionalString(json, "tagFilter");
  query.flagFilter = optionalString(json, "flagFilter");
  if (const Json *columns = member(json, "columnFilters")) {
    requireObject(*columns, "columnFilters");
    query.columnFilters.id = optionalString(*columns, "id");
    query.columnFilters.title = optionalString(*columns, "title");
    query.columnFilters.disambiguation =
        optionalString(*columns, "disambiguation");
    query.columnFilters.alias = optionalString(*columns, "alias");
  }
  if (!query.columnFilters.id.empty() &&
      !std::all_of(query.columnFilters.id.begin(), query.columnFilters.id.end(),
                   [](char ch) { return ch >= '0' && ch <= '9'; }))
    badRequest("The ID column filter accepts digits only.");
  if (const Json *values = member(json, "valueFilters")) {
    if (!values->is_array())
      badRequest("Field 'valueFilters' must be an array.");
    for (const auto &entry : *values) {
      requireObject(entry, "Value filter");
      ItemValueFilter filter;
      filter.fieldId = optionalId(entry, "fieldId");
      if (filter.fieldId <= 0)
        badRequest("Each value filter needs a positive 'fieldId'.");
      filter.value = optionalString(entry, "value");
      filter.exact = optionalBool(entry, "exact", false);
      if (filter.value.empty())
        continue;
      query.valueFilters.push_back(std::move(filter));
    }
  }
  if (const Json *properties = member(json, "propertyFilters")) {
    if (!properties->is_array())
      badRequest("Field 'propertyFilters' must be an array.");
    for (const auto &entry : *properties) {
      requireObject(entry, "Property filter");
      query.propertyFilters.push_back(
          {requiredString(entry, "key"), optionalString(entry, "value")});
    }
  }
  if (const Json *understanding = member(json, "understandingFilter")) {
    if (!understanding->is_string())
      badRequest("Field 'understandingFilter' must be a name or null.");
    query.understandingFilter = static_cast<int>(requiredEnum<UnderstandingLevel>(
        json, "understandingFilter", understandingFromName,
        "understanding level"));
  }
  if (const Json *status = member(json, "statusFilter")) {
    if (!status->is_string())
      badRequest("Field 'statusFilter' must be a name or null.");
    query.statusFilter = static_cast<int>(requiredEnum<ItemStatus>(
        json, "statusFilter", itemStatusFromName, "item status"));
  }
  if (const Json *pinned = member(json, "pinnedFilter")) {
    if (!pinned->is_boolean())
      badRequest("Field 'pinnedFilter' must be a boolean or null.");
    query.pinnedFilter = pinned->get<bool>() ? 1 : 0;
  }
  query.limit = optionalInt(json, "limit", -1);
  query.offset = optionalInt(json, "offset", 0);
  if (query.limit == 0 || query.limit < -1)
    badRequest("Field 'limit' must be positive or -1 for no limit.");
  if (query.limit > 1000)
    badRequest("Field 'limit' must not exceed 1000.");
  if (query.offset < 0)
    badRequest("Field 'offset' must not be negative.");
  query.sortColumn = optionalInt(json, "sortColumn", 3);
  if (query.sortColumn < 0 || query.sortColumn > 1000)
    badRequest("Field 'sortColumn' is out of range.");
  if (member(json, "sortOrder"))
    query.sortOrder = requiredEnum<SortOrder>(json, "sortOrder",
                                              sortOrderFromName, "sort order");
  return query;
}
} // namespace lexicon::http
