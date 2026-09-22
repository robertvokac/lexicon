#pragma once
// Command line and runtime configuration for LexiconServer.
#include "AuthState.h"
#include "Result.h"

#include <cstddef>
#include <string>
#include <vector>

namespace lexicon::http {
struct ServerConfig {
  std::string databasePath = "lexicon.db";
  // Defaults to <database directory>/lexicon-auth.json.
  std::string authFilePath;
  // Where sessions are kept so they survive a restart. Defaults to
  // <database directory>/lexicon-sessions.json.
  std::string sessionFilePath;
  bool persistSessions = true;
  std::string listenAddress = "127.0.0.1";
  int port = 8628;
  std::string tlsCertificatePath;
  std::string tlsPrivateKeyPath;
  bool allowInsecureHttp = false;
  std::vector<std::string> allowedOrigins;
  std::vector<std::string> trustedProxies;
  SessionPolicy sessions;
  LoginLimitPolicy loginLimits;
  std::size_t maxJsonBytes = 1024 * 1024;
  std::size_t maxBlobBytes = 64ull * 1024 * 1024;
  int readTimeoutSeconds = 15;
  int writeTimeoutSeconds = 15;
  int keepAliveTimeoutSeconds = 5;
  std::size_t keepAliveMaxCount = 20;
  bool requestLogging = true;

  bool tlsEnabled() const {
    return !tlsCertificatePath.empty() && !tlsPrivateKeyPath.empty();
  }
  std::string resolvedAuthFilePath() const;
  std::string resolvedSessionFilePath() const;
};

// Returns true for 127.0.0.0/8, ::1 and localhost.
bool isLoopbackAddress(const std::string &address);
// Refuses a configuration that would expose password authentication over
// plaintext HTTP, or that is internally inconsistent.
Result<void> validate(const ServerConfig &config);

enum class Command { Serve, AuthSetUser, AuthShow, Help, Version };

struct CommandLine {
  Command command = Command::Serve;
  ServerConfig config;
};

// Parses argv. Unknown options and malformed values fail with a clear message.
Result<CommandLine> parseCommandLine(const std::vector<std::string> &arguments);
std::string usageText();
} // namespace lexicon::http
