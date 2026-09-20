#include "ServerHarness.h"

#include <chrono>
#include <iostream>

namespace lexicontest {
namespace fs = std::filesystem;

ServerHarness::ServerHarness(HarnessOptions options)
    : options_(std::move(options)) {
  const auto unique =
      std::chrono::steady_clock::now().time_since_epoch().count();
  directory_ = fs::temp_directory_path() /
               ("lexicon-server-test-" + std::to_string(unique));
  std::error_code error;
  fs::create_directories(directory_, error);
  if (error) {
    startupError_ = "Cannot create the temporary directory.";
    return;
  }
  databasePath_ = (directory_ / "lexicon.db").string();
  if (auto opened = repository_.open(databasePath_); !opened) {
    startupError_ = opened.error().message;
    return;
  }
  application_ = std::make_unique<lexicon::LexiconApplication>(repository_);
  auth_ = std::make_unique<lexicon::http::AuthState>(options_.sessions,
                                                     options_.loginLimits);
  if (options_.configureCredentials) {
    auto hashed = lexicon::http::hashPassword(
        options_.password, lexicon::http::ScryptParameters::forTests());
    if (!hashed) {
      startupError_ = hashed.error().message;
      return;
    }
    auth_->setCredentials({options_.username, *hashed});
  }

  lexicon::http::ServerConfig config;
  config.databasePath = databasePath_;
  config.listenAddress = "127.0.0.1";
  config.port = 0; // The operating system picks a free port.
  config.allowedOrigins = options_.allowedOrigins;
  config.sessions = options_.sessions;
  config.loginLimits = options_.loginLimits;
  config.maxJsonBytes = options_.maxJsonBytes;
  config.maxBlobBytes = options_.maxBlobBytes;
  config.requestLogging = false;

  server_ = std::make_unique<lexicon::http::RestServer>(config, *application_,
                                                        *auth_);
  auto bound = server_->bind();
  if (!bound) {
    startupError_ = bound.error().message;
    return;
  }
  port_ = *bound;
  worker_ = std::thread([this] {
    if (auto served = server_->listen(); !served)
      std::cerr << "harness: " << served.error().message << '\n';
  });
  server_->waitUntilReady();
  started_ = true;
}

ServerHarness::~ServerHarness() {
  if (server_)
    server_->stop();
  if (worker_.joinable())
    worker_.join();
  server_.reset();
  application_.reset();
  auth_.reset();
  std::error_code ignored;
  fs::remove_all(directory_, ignored);
}

void Checks::expect(bool condition, const std::string &what) {
  if (condition)
    return;
  ++failures_;
  std::cerr << "FAIL: " << what << '\n';
}

void Checks::expectEqual(long long actual, long long expected,
                         const std::string &what) {
  if (actual == expected)
    return;
  ++failures_;
  std::cerr << "FAIL: " << what << " (expected " << expected << ", got "
            << actual << ")\n";
}

void Checks::expectEqual(const std::string &actual, const std::string &expected,
                         const std::string &what) {
  if (actual == expected)
    return;
  ++failures_;
  std::cerr << "FAIL: " << what << " (expected '" << expected << "', got '"
            << actual << "')\n";
}

int Checks::summarize(const char *suite) const {
  if (failures_ == 0) {
    std::cout << suite << ": all checks passed\n";
    return 0;
  }
  std::cerr << suite << ": " << failures_ << " check(s) failed\n";
  return 1;
}
} // namespace lexicontest
