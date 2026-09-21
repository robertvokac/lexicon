#include "Utf8Path.h"

#include <cstring>

namespace lexicon {
// std::string and std::u8string hold the same bytes in different character
// containers, so the bytes are copied rather than reinterpreted: char8_t is
// not char, and pointer punning between them is not something to rely on.
std::filesystem::path utf8Path(std::string_view value) {
  std::u8string text(value.size(), u8'\0');
  if (!value.empty())
    std::memcpy(text.data(), value.data(), value.size());
  return std::filesystem::path(std::move(text));
}

std::string pathToUtf8(const std::filesystem::path &value) {
  const auto text = value.u8string();
  std::string result(text.size(), '\0');
  if (!text.empty())
    std::memcpy(result.data(), text.data(), text.size());
  return result;
}
} // namespace lexicon
