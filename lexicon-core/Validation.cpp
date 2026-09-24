#include "Validation.h"
#include "ImageValue.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <regex>
#include <set>

namespace {
lexicon::Result<void> invalid(std::string message) {
  return std::unexpected(
      lexicon::Error{lexicon::Error::Code::Validation, std::move(message)});
}

bool dateValid(std::string_view value) {
  static const std::regex pattern(R"(^(\d{4})-(\d{2})-(\d{2})$)");
  std::cmatch match;
  const std::string text(value);
  if (!std::regex_match(text.c_str(), match, pattern))
    return false;
  const auto year = std::chrono::year(std::stoi(match[1]));
  const auto month =
      std::chrono::month(static_cast<unsigned>(std::stoi(match[2])));
  const auto day = std::chrono::day(static_cast<unsigned>(std::stoi(match[3])));
  return (year / month / day).ok();
}

bool timeValid(std::string_view value) {
  static const std::regex pattern(
      R"(^(\d{2}):(\d{2})(?::(\d{2})(?:\.\d{1,3})?)?$)");
  std::cmatch match;
  const std::string text(value);
  if (!std::regex_match(text.c_str(), match, pattern))
    return false;
  return std::stoi(match[1]) < 24 && std::stoi(match[2]) < 60 &&
         (!match[3].matched || std::stoi(match[3]) < 60);
}
} // namespace

namespace lexicon {
std::string asciiFold(std::string_view value) {
  std::string folded(value);
  for (char &byte : folded)
    if (byte >= 'A' && byte <= 'Z') byte = static_cast<char>(byte + ('a' - 'A'));
  return folded;
}
std::string trim(std::string_view value) {
  const auto whitespace = [](char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
           ch == '\f' || ch == '\v';
  };
  while (!value.empty() && whitespace(value.front()))
    value.remove_prefix(1);
  while (!value.empty() && whitespace(value.back()))
    value.remove_suffix(1);
  return std::string(value);
}

std::vector<std::string>
cleanedUniqueValues(const std::vector<std::string> &values) {
  std::set<std::string> seen;
  std::vector<std::string> result;
  for (const auto &raw : values) {
    const auto value = trim(raw);
    if (!value.empty() && seen.insert(asciiFold(value)).second)
      result.push_back(value);
  }
  std::sort(result.begin(), result.end());
  return result;
}

bool validFieldValue(const ItemFieldRecord &field, std::string_view value) {
  const std::string text(value);
  switch (field.dataType) {
  case FieldDataType::Integer: {
    std::string number = trim(value);
    if (!number.empty() && number.front() == '+')
      number.erase(0, 1);
    long long parsed = 0;
    auto [end, error] =
        std::from_chars(number.data(), number.data() + number.size(), parsed);
    return error == std::errc{} && end == number.data() + number.size();
  }
  case FieldDataType::Float: {
    std::string number = trim(value);
    if (!number.empty() && number.front() == '+')
      number.erase(0, 1);
    double parsed = 0;
    auto [end, error] =
        std::from_chars(number.data(), number.data() + number.size(), parsed);
    return error == std::errc{} && end == number.data() + number.size() &&
           std::isfinite(parsed);
  }
  case FieldDataType::Date:
    return dateValid(value);
  case FieldDataType::Time:
    return timeValid(value);
  case FieldDataType::Timestamp: {
    static const std::regex pattern(
        R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}(?::\d{2}(?:\.\d{1,3})?)?(?:Z|[+-]\d{2}:\d{2})?$)");
    if (!std::regex_match(text, pattern))
      return false;
    return dateValid(value.substr(0, 10)) &&
           timeValid(value.substr(11, value.find_first_of("Z+-", 11) - 11));
  }
  case FieldDataType::Boolean:
    return value == "true" || value == "false";
  case FieldDataType::Enum:
    return std::find(field.enumOptions.begin(), field.enumOptions.end(),
                     text) != field.enumOptions.end();
  case FieldDataType::Blob: {
    static const std::regex pattern("^[0-9a-f]{64}$");
    return std::regex_match(text, pattern);
  }
  case FieldDataType::Image:
    return parseImageValue(value).has_value();
  case FieldDataType::Text:
  case FieldDataType::Other:
    return true;
  }
  return false;
}

Result<void> validateGroup(const GroupRecord &group) {
  if (trim(group.name).empty())
    return invalid("Group name cannot be empty.");
  return {};
}
Result<void> validateType(const ItemTypeRecord &type) {
  if (trim(type.name).empty())
    return invalid("Type name cannot be empty.");
  return {};
}
Result<void> validateField(const ItemFieldRecord &field) {
  const int kind = static_cast<int>(field.dataType);
  if (trim(field.name).empty() || kind < 0 ||
      kind > static_cast<int>(FieldDataType::Image))
    return invalid("Field name or data type is invalid.");
  if (field.dataType == FieldDataType::Enum &&
      cleanedUniqueValues(field.enumOptions).empty())
    return invalid("Enum fields need at least one option.");
  return {};
}
Result<void> validateItem(const ItemRecord &item,
                          const std::vector<ItemFieldRecord> &fields) {
  if (trim(item.title).empty())
    return invalid("Item title cannot be empty.");
  if (item.groupId <= 0)
    return invalid("Item group is required.");
  std::set<std::string> keys;
  for (const auto &property : item.properties) {
    const auto key = asciiFold(trim(property.key));
    if (key.empty() || !keys.insert(key).second)
      return invalid(
          "Property keys must be nonempty and unique within an item.");
  }
  if (item.itemTypeId <= 0 && !item.fieldValues.empty())
    return invalid("An item without a type cannot have field values.");
  for (const auto &[id, value] : item.fieldValues) {
    const auto field =
        std::find_if(fields.begin(), fields.end(),
                     [id](const auto &current) { return current.id == id; });
    if (field == fields.end() || !validFieldValue(*field, value))
      return invalid("Invalid value for item field " + std::to_string(id) +
                     ".");
  }
  return {};
}
std::string normalizedUtcTime(std::string_view text) {
  static const std::regex pattern(R"(^(\d{4}-\d{2}-\d{2})T(\d{2}:\d{2})(:\d{2})?Z$)");
  std::cmatch match;
  const std::string value(text);
  if (!std::regex_match(value.c_str(), match, pattern) || !dateValid(match.str(1)) ||
      !timeValid(match.str(2) + (match[3].matched ? match.str(3) : std::string())))
    return {};
  return match.str(1) + "T" + match.str(2) + (match[3].matched ? match.str(3) : ":00") + "Z";
}
Result<void> validateAlarm(const AlarmRecord &alarm) {
  if (trim(alarm.title).empty())
    return invalid("Alarm title cannot be empty.");
  if (normalizedUtcTime(alarm.firesAt).empty())
    return invalid("An alarm needs a time as UTC YYYY-MM-DDTHH:MM:SSZ.");
  return {};
}
Result<void> validateCardText(std::string_view question, std::string_view answer) {
  if (trim(question).empty())
    return invalid("A card needs a question.");
  if (trim(answer).empty())
    return invalid("A card needs an answer.");
  return {};
}
Result<void> validateCard(const CardRecord &card) {
  if (card.itemId <= 0)
    return invalid("A card belongs to an item.");
  if (auto text = validateCardText(card.question, card.answer); !text)
    return text;
  if (card.successCount < 0 || card.failureCount < 0)
    return invalid("A card cannot count fewer than zero answers.");
  if (!card.lastAttempt.empty() && normalizedUtcTime(card.lastAttempt).empty())
    return invalid("A card's last attempt is a UTC time YYYY-MM-DDTHH:MM:SSZ.");
  // Never answered and no last attempt go together; only an import could
  // bring one without the other.
  if ((card.successCount > 0 || card.failureCount > 0) == card.lastAttempt.empty())
    return invalid("A card has a last attempt exactly when it has been answered.");
  return {};
}
Result<void> validateLink(const LinkRecord &link) {
  if (link.fromItemId <= 0 || link.toItemId <= 0)
    return invalid("Both link endpoints are required.");
  const int type = static_cast<int>(link.linkType);
  if (type <= static_cast<int>(LinkType::None) ||
      type > static_cast<int>(LinkType::Custom))
    return invalid("A valid link type is required.");
  if (link.linkType == LinkType::Custom && trim(link.customValue).empty())
    return invalid("Custom links need a value.");
  return {};
}
} // namespace lexicon
