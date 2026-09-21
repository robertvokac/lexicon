#pragma once
// Operating system text that is not already UTF-8: the Windows command line
// and the Windows console.
//
// Paths themselves are converted by lexicon::utf8Path and lexicon::pathToUtf8
// in core, which both this layer and the SQLite storage use.
#include "Result.h"
#include "Utf8Path.h"

#include <string>
#include <string_view>
#include <vector>

namespace lexicon::http {
// UTF-16 to UTF-8, including surrogate pairs. Deliberately not
// WideCharToMultiByte: this way the conversion is the same code on every
// platform and the tests that cover it run everywhere, rather than only
// where a Windows API is available. Unpaired surrogates are rejected instead
// of being replaced, because text that cannot be spelled exactly is a
// mistake worth reporting.
Result<std::string> utf16ToUtf8(std::u16string_view value);

#ifdef _WIN32
// The same conversion for the platform's wide strings, which are UTF-16 here.
Result<std::string> wideToUtf8(std::wstring_view value);
#endif

// The program arguments as UTF-8, excluding argv[0]. On Windows the narrow
// argv is the active code page, so the wide command line is used instead.
Result<std::vector<std::string>> commandLineArguments(int argc, char **argv);
} // namespace lexicon::http
