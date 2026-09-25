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
  // The static web client (lexicon-web), served read-only under /web on this
  // same port. Off while empty.
  std::string webDirectory;
  // Automatic backups: off while empty.
  std::string backupDirectory;
  int backupIntervalHours = 24;
  int backupKeep = 14;

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
// Refuses a backup directory inside the Blob store, where its files would be
// taken for Blobs.
Result<void> validateBackupDirectory(const ServerConfig &config);
// Refuses a web directory that is not a copy of lexicon-web, or that holds
// the database, the credentials, the sessions or the Blobs - everything in it
// is served to anyone who can reach the port.
Result<void> validateWebDirectory(const ServerConfig &config);

enum class Command { Serve, AuthSetUser, AuthShow, Export, Import, Backup, VerifyBackup, Help, Version };

struct CommandLine {
  Command command = Command::Serve;
  ServerConfig config;
  // export: the file to write, empty for standard output; import: the file
  // to read.
  std::string exchangePath;
  // verify-backup: the completed backup directory to inspect.
  std::string verifyBackupPath;
  // export: whether the files values refer to travel in the document.
  bool exchangeFiles = false;
};

// Parses argv. Unknown options and malformed values fail with a clear message.
Result<CommandLine> parseCommandLine(const std::vector<std::string> &arguments);
std::string usageText();
} // namespace lexicon::http
