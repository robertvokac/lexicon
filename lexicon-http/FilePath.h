#pragma once
// The one place the server layer converts between file system paths and the
// UTF-8 std::string everything else uses.
//
// std::filesystem::path::string() is the *system narrow* encoding, which on
// Windows is the active code page, not UTF-8. Mixing the two turns a database
// in C:\Users\Jiri\Lexicon into a different path, or no path at all, once the
// user name contains a character the code page cannot spell. Only u8string()
// and a u8string-built path are exact on every platform, so those are what
// these two functions use. lexicon-storage-sqlite already works this way.
#include <filesystem>
#include <string>
#include <string_view>

namespace lexicon::http {
std::filesystem::path fromUtf8(std::string_view value);
std::string toUtf8(const std::filesystem::path &value);
} // namespace lexicon::http
