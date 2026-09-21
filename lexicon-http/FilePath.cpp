#include "FilePath.h"

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace lexicon::http {
namespace {
std::unexpected<Error> invalid(std::string message) {
  return std::unexpected(Error{Error::Code::Validation, std::move(message)});
}
} // namespace

Result<std::string> utf16ToUtf8(std::u16string_view value) {
  std::string result;
  result.reserve(value.size());
  for (std::size_t index = 0; index < value.size(); ++index) {
    char32_t code = value[index];
    if (code >= 0xd800 && code <= 0xdbff) {
      // A high surrogate must be followed by its low half.
      if (index + 1 >= value.size() || value[index + 1] < 0xdc00 ||
          value[index + 1] > 0xdfff)
        return invalid("The text contains an unpaired UTF-16 surrogate.");
      code = 0x10000 + ((code - 0xd800) << 10) + (value[++index] - 0xdc00);
    } else if (code >= 0xdc00 && code <= 0xdfff) {
      return invalid("The text contains an unpaired UTF-16 surrogate.");
    }
    const auto append = [&result](char32_t byte) {
      result.push_back(static_cast<char>(static_cast<unsigned char>(byte)));
    };
    if (code < 0x80) {
      append(code);
    } else if (code < 0x800) {
      append(0xc0 | (code >> 6));
      append(0x80 | (code & 0x3f));
    } else if (code < 0x10000) {
      append(0xe0 | (code >> 12));
      append(0x80 | ((code >> 6) & 0x3f));
      append(0x80 | (code & 0x3f));
    } else {
      append(0xf0 | (code >> 18));
      append(0x80 | ((code >> 12) & 0x3f));
      append(0x80 | ((code >> 6) & 0x3f));
      append(0x80 | (code & 0x3f));
    }
  }
  return result;
}

#ifdef _WIN32
Result<std::string> wideToUtf8(std::wstring_view value) {
  // wchar_t is 16 bit here; copying element by element keeps the values and
  // avoids reinterpreting one character type as another.
  std::u16string text;
  text.reserve(value.size());
  for (const wchar_t unit : value)
    text.push_back(static_cast<char16_t>(unit));
  return utf16ToUtf8(text);
}
#endif

Result<std::vector<std::string>> commandLineArguments(int argc, char **argv) {
#ifdef _WIN32
  static_cast<void>(argc);
  static_cast<void>(argv);
  int count = 0;
  LPWSTR *wide = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!wide)
    return std::unexpected(
        Error{Error::Code::Validation, "Cannot read the Windows command line."});
  struct Release {
    LPWSTR *value;
    ~Release() { LocalFree(value); }
  } release{wide};
  std::vector<std::string> arguments;
  for (int index = 1; index < count; ++index) {
    auto converted = wideToUtf8(wide[index]);
    if (!converted)
      return std::unexpected(
          Error{converted.error().code,
                "Cannot convert the command line to UTF-8: " +
                    converted.error().message});
    arguments.push_back(std::move(*converted));
  }
  return arguments;
#else
  // POSIX hands over the bytes the user typed, which are already UTF-8 on any
  // current system.
  return std::vector<std::string>(argv + 1, argv + argc);
#endif
}
} // namespace lexicon::http
