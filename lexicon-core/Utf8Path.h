#pragma once
// The conversion between a UTF-8 std::string and a file system path.
//
// All text crossing the core boundary is UTF-8, including paths, so this is
// the completion of that policy rather than a new one. It lives in core
// because both the SQLite storage and the HTTP server need exactly the same
// conversion and neither may depend on the other.
//
// std::filesystem::path::string() is the *native narrow* encoding, which the
// standard does not define as UTF-8 and which MSVC maps to the active code
// page. Only u8string() and a u8string-built path are exact everywhere.
#include <filesystem>
#include <string>
#include <string_view>

namespace lexicon {
std::filesystem::path utf8Path(std::string_view value);
std::string pathToUtf8(const std::filesystem::path &value);
} // namespace lexicon
