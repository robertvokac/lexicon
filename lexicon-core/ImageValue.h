#pragma once
#include "Records.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace lexicon {
// An Image value names a stored file and says what kind of image it is:
// "<media type>:<SHA-256 of the file>", such as "image/png:6c7d...".
struct ImageValue {
  std::string mediaType;
  std::string hash;
};

// The image types Lexicon stores and shows. No SVG: it can carry script.
inline constexpr std::array<std::string_view, 5> kImageMediaTypes{
    "image/png", "image/jpeg", "image/gif", "image/webp", "image/bmp"};

std::optional<ImageValue> parseImageValue(std::string_view value);
std::string formatImageValue(std::string_view mediaType, std::string_view hash);
// The image type the bytes begin with, or "" when they begin none of
// kImageMediaTypes. The first 12 bytes are enough.
std::string sniffImageType(std::string_view head);
// "png", "jpg", "gif", "webp" or "bmp"; "" for anything else.
std::string imageExtension(std::string_view mediaType);
// The SHA-256 of the stored file a Blob or Image value refers to, or "" for
// any other kind of field.
std::string storedFileHash(FieldDataType type, std::string_view value);
} // namespace lexicon
