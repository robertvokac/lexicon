#pragma once

#include <expected>
#include <string>

namespace lexicon {
struct Error {
  // Conflict: the record changed since the caller loaded it.
  enum class Code { Validation, NotFound, Storage, Conflict };
  Code code;
  std::string message;
};

template <class T> using Result = std::expected<T, Error>;
} // namespace lexicon
