#pragma once
// Single-user authentication state for the REST server. This is server
// infrastructure, not part of the Lexicon knowledge domain: lexicon-core never
// learns that a password exists.
#include "Security.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <semaphore>
#include <string>

namespace lexicon::http {
struct Credentials {
  std::string username;
  PasswordHash password;
};

// Credentials live in their own file next to the database so that historical
// Lexicon databases stay readable by the Qt client.
Result<Credentials> readCredentialsFile(const std::string &path);
Result<void> writeCredentialsFile(const std::string &path,
                                  const Credentials &credentials);

struct SessionPolicy {
  std::chrono::seconds idleTimeout{8 * 60 * 60};
  std::chrono::seconds absoluteLifetime{7 * 24 * 60 * 60};
  std::size_t maxSessions = 32;
};

struct LoginLimitPolicy {
  int maxFailuresPerClient = 10;
  std::chrono::seconds window{15 * 60};
  // Bounds the tracking table so a client cannot grow server memory.
  std::size_t maxTrackedClients = 4096;
  // Backstop against attackers rotating source addresses.
  int maxFailuresTotal = 200;
  // Password hashing is deliberately expensive in CPU *and* memory, so only a
  // few derivations may run at once. Without this, a burst of logins would
  // multiply the scrypt working set by the number of requests.
  int maxConcurrentHashes = 2;
};

class AuthState {
public:
  AuthState(SessionPolicy sessions, LoginLimitPolicy limits);

  void setCredentials(Credentials credentials);
  bool configured() const;
  std::string username() const;

  enum class LoginStatus { Ok, InvalidCredentials, RateLimited, Unavailable };
  struct LoginResult {
    LoginStatus status = LoginStatus::InvalidCredentials;
    std::string token;
    int retryAfterSeconds = 0;
  };
  // clientKey identifies the caller for rate limiting. It is never logged.
  LoginResult login(const std::string &clientKey, const std::string &username,
                    const std::string &password);

  // Returns the authenticated user name, refreshing the idle timer.
  std::optional<std::string> authenticate(const std::string &token);
  bool logout(const std::string &token);
  std::size_t sessionCount() const;

  // Test hook: shifts the internal clock so expiry and rate-limit windows can
  // be exercised without sleeping.
  void advanceClockForTests(std::chrono::seconds amount);
  // The largest number of password derivations that have ever overlapped.
  // Observability for the bound above, and what the tests assert on.
  int peakPasswordHashConcurrency() const;

  // The upper bound the semaphore is sized for; the policy is clamped to it.
  static constexpr std::ptrdiff_t maxHashSlots = 32;

private:
  using Clock = std::chrono::steady_clock;
  struct Session {
    std::string username;
    Clock::time_point created;
    Clock::time_point lastSeen;
  };
  struct FailureCounter {
    int failures = 0;
    Clock::time_point firstFailure;
    Clock::time_point lastFailure;
  };

  // Holds one of the bounded password-hashing slots for its lifetime. The
  // session mutex is deliberately *not* held while it is alive: an expensive
  // derivation must never delay authenticating an existing session.
  class HashPermit {
  public:
    explicit HashPermit(AuthState &state);
    ~HashPermit();
    HashPermit(const HashPermit &) = delete;
    HashPermit &operator=(const HashPermit &) = delete;

  private:
    AuthState &state_;
  };

  Clock::time_point now() const;
  void expireSessions(Clock::time_point moment);
  bool limited(const std::string &clientKey, Clock::time_point moment,
               int &retryAfterSeconds);
  void recordFailure(const std::string &clientKey, Clock::time_point moment);
  void clearFailures(const std::string &clientKey);

  mutable std::mutex mutex_;
  SessionPolicy sessions_;
  LoginLimitPolicy limits_;
  std::optional<Credentials> credentials_;
  // Keyed by the SHA-256 of the token; raw tokens are never kept.
  std::map<std::string, Session> activeSessions_;
  std::map<std::string, FailureCounter> failures_;
  FailureCounter totalFailures_;
  std::chrono::seconds testOffset_{0};

  // Independent of mutex_ on purpose: these are touched while no lock is held.
  std::counting_semaphore<maxHashSlots> hashSlots_;
  std::atomic<int> activeHashes_{0};
  std::atomic<int> peakHashes_{0};
};
} // namespace lexicon::http
