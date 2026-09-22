// Image values and how an image is recognised by its first bytes.
#include "ImageValue.h"
#include "Validation.h"

#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool condition, const std::string &message) {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}
const std::string kHash(64, 'a');
} // namespace

int main() {
  using namespace lexicon;
  using namespace std::string_literals;

  check(sniffImageType("\x89PNG\r\n\x1a\n\0\0\0\rIHDR"s) == "image/png", "PNG");
  check(sniffImageType("\xff\xd8\xff\xe0\0\x10JFIF"s) == "image/jpeg", "JPEG");
  check(sniffImageType("GIF89a\x01\0\x01\0"s) == "image/gif" && sniffImageType("GIF87a....") == "image/gif", "GIF");
  check(sniffImageType("RIFF\x24\0\0\0WEBPVP8 "s) == "image/webp", "WebP");
  check(sniffImageType("RIFF\x24\0\0\0WAVEfmt "s).empty(), "a WAV file is RIFF but no image");
  check(sniffImageType("BM\x46\0\0\0\0\0\0\0\x36\0\0\0"s) == "image/bmp", "BMP");
  check(sniffImageType("BM").empty(), "two letters are not yet a bitmap");
  check(sniffImageType("<svg xmlns=\"http://www.w3.org/2000/svg\">").empty(), "SVG is not shown as an image");
  check(sniffImageType("").empty() && sniffImageType("\x89PN").empty(), "too short to tell");

  const auto parsed = parseImageValue("image/png:" + kHash);
  check(parsed && parsed->mediaType == "image/png" && parsed->hash == kHash, "a value names its type and file");
  check(formatImageValue("image/webp", kHash) == "image/webp:" + kHash, "and is written the same way");
  check(!parseImageValue(kHash), "a bare hash is no image value");
  check(!parseImageValue("image/svg+xml:" + kHash), "SVG is refused");
  check(!parseImageValue("image/png:" + std::string(64, 'A')), "the hash is lowercase");
  check(!parseImageValue("image/png:" + kHash.substr(1)), "and complete");
  check(!parseImageValue("image/png:" + kHash + " "), "without trailing text");

  check(storedFileHash(FieldDataType::Image, "image/gif:" + kHash) == kHash, "an image refers to its file");
  check(storedFileHash(FieldDataType::Blob, kHash) == kHash, "so does a blob");
  check(storedFileHash(FieldDataType::Text, kHash).empty(), "text refers to no file");
  check(storedFileHash(FieldDataType::Image, "garbage").empty(), "a broken image value to none");
  check(imageExtension("image/jpeg") == "jpg" && imageExtension("text/plain").empty(), "extensions");

  ItemFieldRecord field;
  field.name = "Picture";
  field.dataType = FieldDataType::Image;
  check(validateField(field).has_value(), "an Image field is valid");
  check(validFieldValue(field, "image/jpeg:" + kHash) && !validFieldValue(field, kHash), "its values are checked");

  if (failures == 0) std::cout << "image_value: all checks passed\n";
  return failures == 0 ? 0 : 1;
}
