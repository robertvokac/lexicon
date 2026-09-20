#pragma once

#include <expected>
#include <string>

namespace lexicon {
struct Error {
  enum class Code { Validation, NotFound, Storage };
  Code code;
  std::string message;
};

template <class T> using Result = std::expected<T, Error>;
} // namespace lexicon
