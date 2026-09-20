#pragma once
// Cryptographic primitives for the server: password hashing, session tokens,
// encodings and constant-time comparison. OpenSSL is the only dependency.
#include "Result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lexicon::http {
using Bytes = std::vector<unsigned char>;

// scrypt work factors. Stored with every hash so they can be raised later
// without invalidating existing credentials.
struct ScryptParameters {
  std::uint64_t n = 1u << 16; // CPU/memory cost, a power of two
  std::uint32_t r = 8;
  std::uint32_t p = 1;
  std::uint32_t keyLength = 32;
  // 2 * 128 * r * n bytes of headroom keeps OpenSSL from refusing the cost.
  std::uint64_t maxMemory = 512ull * 1024 * 1024;
  // Deliberately cheap parameters for tests. Never used by the CLI.
  static ScryptParameters forTests() {
    ScryptParameters parameters;
    parameters.n = 1024;
    parameters.maxMemory = 32ull * 1024 * 1024;
    return parameters;
  }
};

// A versioned password record. Version 1 is scrypt with a random salt.
struct PasswordHash {
  int version = 1;
  std::string algorithm = "scrypt";
  ScryptParameters parameters;
  Bytes salt;
  Bytes hash;
};

Result<Bytes> randomBytes(std::size_t count);
// 256 bits of CSPRNG material encoded as unpadded base64url.
Result<std::string> randomToken();

std::string base64Encode(const Bytes &bytes);
Result<Bytes> base64Decode(std::string_view text);
std::string base64UrlEncode(const Bytes &bytes);
// Lowercase hexadecimal SHA-256, used to key session tokens without storing
// them.
std::string sha256Hex(std::string_view text);

bool constantTimeEquals(std::string_view left, std::string_view right);
bool constantTimeEquals(const Bytes &left, const Bytes &right);

Result<PasswordHash> hashPassword(std::string_view password,
                                 const ScryptParameters &parameters);
// Always performs the full derivation so that a wrong password costs the same
// as a right one.
Result<bool> verifyPassword(const PasswordHash &stored,
                            std::string_view password);
} // namespace lexicon::http
