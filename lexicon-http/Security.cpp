#include "Security.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

#include <array>

namespace lexicon::http {
namespace {
constexpr char kStandardAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr char kUrlAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::unexpected<Error> failure(std::string message) {
  return std::unexpected(Error{Error::Code::Storage, std::move(message)});
}

std::string encode(const Bytes &bytes, const char *alphabet, bool pad) {
  std::string text;
  text.reserve((bytes.size() + 2) / 3 * 4);
  for (std::size_t i = 0; i < bytes.size(); i += 3) {
    const unsigned first = bytes[i];
    const unsigned second = i + 1 < bytes.size() ? bytes[i + 1] : 0u;
    const unsigned third = i + 2 < bytes.size() ? bytes[i + 2] : 0u;
    const unsigned group = (first << 16) | (second << 8) | third;
    text += alphabet[(group >> 18) & 0x3f];
    text += alphabet[(group >> 12) & 0x3f];
    if (i + 1 < bytes.size())
      text += alphabet[(group >> 6) & 0x3f];
    else if (pad)
      text += '=';
    if (i + 2 < bytes.size())
      text += alphabet[group & 0x3f];
    else if (pad)
      text += '=';
  }
  return text;
}
} // namespace

Result<Bytes> randomBytes(std::size_t count) {
  Bytes bytes(count);
  if (count != 0 && RAND_bytes(bytes.data(), static_cast<int>(count)) != 1)
    return failure("The system random number generator failed.");
  return bytes;
}

Result<std::string> randomToken() {
  auto bytes = randomBytes(32);
  if (!bytes)
    return std::unexpected(bytes.error());
  return base64UrlEncode(*bytes);
}

std::string base64Encode(const Bytes &bytes) {
  return encode(bytes, kStandardAlphabet, true);
}

std::string base64UrlEncode(const Bytes &bytes) {
  return encode(bytes, kUrlAlphabet, false);
}

Result<Bytes> base64Decode(std::string_view text) {
  std::array<signed char, 256> reverse{};
  reverse.fill(-1);
  for (int i = 0; i < 64; ++i)
    reverse[static_cast<unsigned char>(kStandardAlphabet[i])] =
        static_cast<signed char>(i);
  Bytes bytes;
  unsigned buffer = 0;
  int bits = 0;
  for (const char character : text) {
    if (character == '=' || character == '\n' || character == '\r')
      continue;
    const signed char value = reverse[static_cast<unsigned char>(character)];
    if (value < 0)
      return std::unexpected(
          Error{Error::Code::Validation, "Invalid base64 content."});
    buffer = (buffer << 6) | static_cast<unsigned>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      bytes.push_back(static_cast<unsigned char>((buffer >> bits) & 0xff));
    }
  }
  return bytes;
}

std::string sha256Hex(std::string_view text) {
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned length = 0;
  if (EVP_Digest(text.data(), text.size(), digest, &length, EVP_sha256(),
                 nullptr) != 1)
    return {};
  static constexpr char digits[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(length * 2);
  for (unsigned i = 0; i < length; ++i) {
    hex += digits[digest[i] >> 4];
    hex += digits[digest[i] & 0x0f];
  }
  return hex;
}

bool constantTimeEquals(std::string_view left, std::string_view right) {
  // Compare fixed-size digests of both values so that neither the length nor
  // the content leaks through the comparison.
  const auto leftDigest = sha256Hex(left);
  const auto rightDigest = sha256Hex(right);
  if (leftDigest.empty() || leftDigest.size() != rightDigest.size())
    return false;
  return CRYPTO_memcmp(leftDigest.data(), rightDigest.data(),
                       leftDigest.size()) == 0;
}

bool constantTimeEquals(const Bytes &left, const Bytes &right) {
  if (left.size() != right.size())
    return false;
  if (left.empty())
    return true;
  return CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
}

namespace {
Result<Bytes> derive(std::string_view password, const Bytes &salt,
                     const ScryptParameters &parameters) {
  if (parameters.keyLength == 0 || parameters.keyLength > 1024)
    return std::unexpected(
        Error{Error::Code::Validation, "Invalid password hash length."});
  if (parameters.n < 2 || (parameters.n & (parameters.n - 1)) != 0 ||
      parameters.r == 0 || parameters.p == 0)
    return std::unexpected(
        Error{Error::Code::Validation, "Invalid scrypt parameters."});
  Bytes key(parameters.keyLength);
  if (EVP_PBE_scrypt(password.data(), password.size(), salt.data(), salt.size(),
                     parameters.n, parameters.r, parameters.p,
                     parameters.maxMemory, key.data(), key.size()) != 1)
    return failure("Password hashing failed.");
  return key;
}
} // namespace

Result<PasswordHash> hashPassword(std::string_view password,
                                 const ScryptParameters &parameters) {
  auto salt = randomBytes(16);
  if (!salt)
    return std::unexpected(salt.error());
  auto key = derive(password, *salt, parameters);
  if (!key)
    return std::unexpected(key.error());
  PasswordHash stored;
  stored.parameters = parameters;
  stored.salt = std::move(*salt);
  stored.hash = std::move(*key);
  return stored;
}

Result<bool> verifyPassword(const PasswordHash &stored,
                            std::string_view password) {
  if (stored.version != 1 || stored.algorithm != "scrypt")
    return std::unexpected(Error{Error::Code::Validation,
                                 "Unsupported password hash version."});
  auto key = derive(password, stored.salt, stored.parameters);
  if (!key)
    return std::unexpected(key.error());
  return constantTimeEquals(*key, stored.hash);
}
} // namespace lexicon::http
