#include "ImageValue.h"

#include <algorithm>

namespace lexicon {
namespace {
bool canonicalHash(std::string_view hash) {
  return hash.size() == 64 &&
         std::all_of(hash.begin(), hash.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
bool knownType(std::string_view mediaType) {
  return std::find(kImageMediaTypes.begin(), kImageMediaTypes.end(), mediaType) != kImageMediaTypes.end();
}
} // namespace

std::optional<ImageValue> parseImageValue(std::string_view value) {
  const auto colon = value.rfind(':');
  if (colon == std::string_view::npos)
    return std::nullopt;
  const auto mediaType = value.substr(0, colon);
  const auto hash = value.substr(colon + 1);
  if (!knownType(mediaType) || !canonicalHash(hash))
    return std::nullopt;
  return ImageValue{std::string(mediaType), std::string(hash)};
}

std::string formatImageValue(std::string_view mediaType, std::string_view hash) {
  return std::string(mediaType) + ":" + std::string(hash);
}

std::string sniffImageType(std::string_view head) {
  const auto starts = [head](std::string_view prefix, std::size_t at = 0) {
    return head.size() >= at + prefix.size() && head.substr(at, prefix.size()) == prefix;
  };
  if (starts("\x89PNG\r\n\x1a\n"))
    return "image/png";
  if (starts("\xff\xd8\xff"))
    return "image/jpeg";
  if (starts("GIF87a") || starts("GIF89a"))
    return "image/gif";
  if (starts("RIFF") && starts("WEBP", 8))
    return "image/webp";
  if (starts("BM") && head.size() >= 14)
    return "image/bmp";
  return {};
}

std::string imageExtension(std::string_view mediaType) {
  if (mediaType == "image/png") return "png";
  if (mediaType == "image/jpeg") return "jpg";
  if (mediaType == "image/gif") return "gif";
  if (mediaType == "image/webp") return "webp";
  if (mediaType == "image/bmp") return "bmp";
  return {};
}

std::string storedFileHash(FieldDataType type, std::string_view value) {
  if (type == FieldDataType::Blob)
    return std::string(value);
  if (type == FieldDataType::Image)
    if (auto image = parseImageValue(value))
      return image->hash;
  return {};
}
} // namespace lexicon
