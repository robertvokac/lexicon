#pragma once
// Starts a real RestServer on an ephemeral port against a temporary database.
#include "AuthState.h"
#include "LexiconApplication.h"
#include "RestServer.h"
#include "ServerConfig.h"
#include "SqliteRepository.h"

#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace lexicontest {
// Writes a short-lived self-signed certificate and key so the TLS listener can
// be exercised without any external tool.
bool writeSelfSignedCertificate(const std::string &certificatePath,
                                const std::string &keyPath);

struct HarnessOptions {
  std::vector<std::string> allowedOrigins;
  bool tls = false;
  lexicon::http::SessionPolicy sessions;
  lexicon::http::LoginLimitPolicy loginLimits;
  std::string username = "lexicon";
  std::string password = "correct-horse-battery-staple";
  bool configureCredentials = true;
  std::size_t maxJsonBytes = 64 * 1024;
  std::size_t maxBlobBytes = 1024 * 1024;
  // Served read-only under /web, as --web-dir does.
  std::string webDirectory;
};

class ServerHarness {
public:
  explicit ServerHarness(HarnessOptions options = {});
  ~ServerHarness();
  ServerHarness(const ServerHarness &) = delete;
  ServerHarness &operator=(const ServerHarness &) = delete;

  bool started() const { return started_; }
  const std::string &startupError() const { return startupError_; }
  int port() const { return port_; }
  const std::string &databasePath() const { return databasePath_; }
  const HarnessOptions &options() const { return options_; }
  lexicon::LexiconApplication &application() { return *application_; }
  lexicon::http::AuthState &auth() { return *auth_; }

private:
  HarnessOptions options_;
  std::filesystem::path directory_;
  std::string databasePath_;
  SqliteRepository repository_;
  std::unique_ptr<lexicon::LexiconApplication> application_;
  std::unique_ptr<lexicon::http::AuthState> auth_;
  std::unique_ptr<lexicon::http::RestServer> server_;
  std::thread worker_;
  int port_ = 0;
  bool started_ = false;
  std::string startupError_;
};

// Minimal assertion helper matching the existing test style.
class Checks {
public:
  void expect(bool condition, const std::string &what);
  void expectEqual(long long actual, long long expected,
                   const std::string &what);
  void expectEqual(const std::string &actual, const std::string &expected,
                   const std::string &what);
  int failures() const { return failures_; }
  int summarize(const char *suite) const;

private:
  int failures_ = 0;
};
} // namespace lexicontest
