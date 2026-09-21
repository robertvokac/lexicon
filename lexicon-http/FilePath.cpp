#include "FilePath.h"

namespace lexicon::http {
std::filesystem::path fromUtf8(std::string_view value) {
  return std::filesystem::path(std::u8string(
      reinterpret_cast<const char8_t *>(value.data()), value.size()));
}

std::string toUtf8(const std::filesystem::path &value) {
  const auto text = value.u8string();
  return std::string(reinterpret_cast<const char *>(text.data()), text.size());
}
} // namespace lexicon::http
