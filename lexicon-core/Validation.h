#pragma once
#include "Records.h"
#include "Result.h"

#include <string_view>

namespace lexicon {
std::string trim(std::string_view value);
std::vector<std::string>
cleanedUniqueValues(const std::vector<std::string> &values);
bool validFieldValue(const ItemFieldRecord &field, std::string_view value);
Result<void> validateGroup(const GroupRecord &group);
Result<void> validateType(const ItemTypeRecord &type);
Result<void> validateField(const ItemFieldRecord &field);
Result<void> validateItem(const ItemRecord &item,
                          const std::vector<ItemFieldRecord> &fields);
Result<void> validateLink(const LinkRecord &link);
} // namespace lexicon
