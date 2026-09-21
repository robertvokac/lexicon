#include "ServerHarness.h"

#include "FilePath.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>

namespace lexicontest {
namespace fs = std::filesystem;

bool writeSelfSignedCertificate(const std::string &certificatePath,
                                const std::string &keyPath) {
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(EVP_RSA_gen(2048),
                                                          EVP_PKEY_free);
  if (!key)
    return false;
  std::unique_ptr<X509, decltype(&X509_free)> certificate(X509_new(), X509_free);
  if (!certificate)
    return false;
  X509_set_version(certificate.get(), 2);
  ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1);
  X509_gmtime_adj(X509_getm_notBefore(certificate.get()), 0);
  X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 60 * 60);
  X509_set_pubkey(certificate.get(), key.get());
  X509_NAME *name = X509_get_subject_name(certificate.get());
  X509_NAME_add_entry_by_txt(
      name, "CN", MBSTRING_ASC,
      reinterpret_cast<const unsigned char *>("localhost"), -1, -1, 0);
  X509_set_issuer_name(certificate.get(), name);
  if (X509_sign(certificate.get(), key.get(), EVP_sha256()) == 0)
    return false;

  struct File {
    std::FILE *handle;
    ~File() {
      if (handle)
        std::fclose(handle);
    }
  };
  File certificateFile{std::fopen(certificatePath.c_str(), "wb")};
  if (!certificateFile.handle ||
      PEM_write_X509(certificateFile.handle, certificate.get()) == 0)
    return false;
  File keyFile{std::fopen(keyPath.c_str(), "wb")};
  if (!keyFile.handle ||
      PEM_write_PrivateKey(keyFile.handle, key.get(), nullptr, nullptr, 0,
                           nullptr, nullptr) == 0)
    return false;
  return true;
}

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
  databasePath_ = lexicon::http::toUtf8(directory_ / "lexicon.db");
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
  if (options_.tls) {
    const auto certificate = lexicon::http::toUtf8(directory_ / "server-cert.pem");
    const auto key = lexicon::http::toUtf8(directory_ / "server-key.pem");
    if (!writeSelfSignedCertificate(certificate, key)) {
      startupError_ = "Cannot create the test certificate.";
      return;
    }
    config.tlsCertificatePath = certificate;
    config.tlsPrivateKeyPath = key;
  }
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
