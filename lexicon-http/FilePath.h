#pragma once
// The one place the server layer converts between operating system text and
// the UTF-8 std::string everything else uses.
//
// Two conversions on Windows are lossy and neither is UTF-8:
//   * std::filesystem::path::string() is the *native narrow* encoding, which
//     with MSVC is the active code page. MinGW's libstdc++ happens to produce
//     UTF-8, which only hides the difference until the toolchain changes.
//   * the narrow argv the C runtime builds is the active code page whatever
//     the compiler, so a path under C:\Users\Jiri with a hacek arrives with
//     the hacek dropped, naming a directory that does not exist.
// Only u8string(), a u8string-built path, and the wide command line are exact,
// so those are what these functions use. lexicon-storage-sqlite already works
// this way.
#include "Result.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace lexicon::http {
std::filesystem::path fromUtf8(std::string_view value);
std::string toUtf8(const std::filesystem::path &value);

// UTF-16 to UTF-8, including surrogate pairs. Deliberately not
// WideCharToMultiByte: this way the conversion is the same code on every
// platform and the tests that cover it run everywhere, rather than only
// where a Windows API is available. Unpaired surrogates are rejected instead
// of being replaced, because a path that cannot be spelled exactly is a
// mistake worth reporting.
Result<std::string> utf16ToUtf8(std::u16string_view value);

// The program arguments as UTF-8, excluding argv[0]. On Windows the narrow
// argv is ignored in favour of the wide command line.
std::vector<std::string> commandLineArguments(int argc, char **argv);
} // namespace lexicon::http
