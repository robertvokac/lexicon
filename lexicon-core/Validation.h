#pragma once
#include "Records.h"
#include "Result.h"

#include <string_view>

namespace lexicon {
std::string trim(std::string_view value);
// ASCII letters are case-insensitive; non-ASCII UTF-8 bytes compare exactly.
std::string asciiFold(std::string_view value);
std::vector<std::string>
cleanedUniqueValues(const std::vector<std::string> &values);
bool validFieldValue(const ItemFieldRecord &field, std::string_view value);
Result<void> validateGroup(const GroupRecord &group);
Result<void> validateType(const ItemTypeRecord &type);
Result<void> validateField(const ItemFieldRecord &field);
Result<void> validateItem(const ItemRecord &item,
                          const std::vector<ItemFieldRecord> &fields);
Result<void> validateLink(const LinkRecord &link);
// "YYYY-MM-DDTHH:MM[:SS]Z" as "YYYY-MM-DDTHH:MM:SSZ", or empty when the text
// is not such a UTC time.
std::string normalizedUtcTime(std::string_view text);
Result<void> validateAlarm(const AlarmRecord &alarm);
} // namespace lexicon
