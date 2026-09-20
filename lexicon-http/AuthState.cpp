#include "AuthState.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace lexicon::http {
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;

std::unexpected<Error> invalid(std::string message) {
  return std::unexpected(Error{Error::Code::Validation, std::move(message)});
}
std::unexpected<Error> storage(std::string message) {
  return std::unexpected(Error{Error::Code::Storage, std::move(message)});
}

fs::path utf8Path(const std::string &value) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t *>(value.data()),
                                value.size()));
}

std::uint64_t positiveNumber(const Json &json, const char *key,
                             std::uint64_t fallback) {
  const auto found = json.find(key);
  if (found == json.end() || !found->is_number_unsigned())
    return fallback;
  return found->get<std::uint64_t>();
}
} // namespace

Result<Credentials> readCredentialsFile(const std::string &path) {
  std::ifstream input(utf8Path(path), std::ios::binary);
  if (!input)
    return std::unexpected(
        Error{Error::Code::NotFound, "No server credentials are configured."});
  Json document;
  try {
    input >> document;
  } catch (const std::exception &) {
    return invalid("The server credentials file is not valid JSON.");
  }
  if (!document.is_object() || positiveNumber(document, "version", 0) != 1)
    return invalid("Unsupported server credentials file version.");
  const auto username = document.find("username");
  const auto password = document.find("password");
  if (username == document.end() || !username->is_string() ||
      password == document.end() || !password->is_object())
    return invalid("The server credentials file is incomplete.");
  Credentials credentials;
  credentials.username = username->get<std::string>();
  const Json &stored = *password;
  const auto algorithm = stored.find("algorithm");
  if (algorithm == stored.end() || !algorithm->is_string() ||
      algorithm->get<std::string>() != "scrypt" ||
      positiveNumber(stored, "version", 0) != 1)
    return invalid("Unsupported password hash in the credentials file.");
  credentials.password.version = 1;
  credentials.password.algorithm = "scrypt";
  ScryptParameters parameters;
  parameters.n = positiveNumber(stored, "n", parameters.n);
  parameters.r = static_cast<std::uint32_t>(positiveNumber(stored, "r", parameters.r));
  parameters.p = static_cast<std::uint32_t>(positiveNumber(stored, "p", parameters.p));
  parameters.keyLength = static_cast<std::uint32_t>(
      positiveNumber(stored, "keyLength", parameters.keyLength));
  parameters.maxMemory = positiveNumber(stored, "maxMemory", parameters.maxMemory);
  credentials.password.parameters = parameters;
  const auto salt = stored.find("salt");
  const auto hash = stored.find("hash");
  if (salt == stored.end() || !salt->is_string() || hash == stored.end() ||
      !hash->is_string())
    return invalid("The stored password hash is incomplete.");
  auto saltBytes = base64Decode(salt->get<std::string>());
  auto hashBytes = base64Decode(hash->get<std::string>());
  if (!saltBytes)
    return std::unexpected(saltBytes.error());
  if (!hashBytes)
    return std::unexpected(hashBytes.error());
  if (saltBytes->size() < 8 || hashBytes->size() < 16)
    return invalid("The stored password hash is too short.");
  credentials.password.salt = std::move(*saltBytes);
  credentials.password.hash = std::move(*hashBytes);
  return credentials;
}

Result<void> writeCredentialsFile(const std::string &path,
                                  const Credentials &credentials) {
  if (credentials.username.empty())
    return invalid("The user name cannot be empty.");
  const Json document{
      {"version", 1},
      {"username", credentials.username},
      {"password",
       {{"algorithm", credentials.password.algorithm},
        {"version", credentials.password.version},
        {"n", credentials.password.parameters.n},
        {"r", credentials.password.parameters.r},
        {"p", credentials.password.parameters.p},
        {"keyLength", credentials.password.parameters.keyLength},
        {"maxMemory", credentials.password.parameters.maxMemory},
        {"salt", base64Encode(credentials.password.salt)},
        {"hash", base64Encode(credentials.password.hash)}}}};

  const auto target = utf8Path(path);
  const auto temporary = fs::path(target).concat(".new");
  std::error_code ignored;
  fs::remove(temporary, ignored);
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output)
      return storage("Cannot write the server credentials file.");
    std::error_code error;
    fs::permissions(temporary,
                    fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, error);
    if (error) {
      output.close();
      fs::remove(temporary, ignored);
      return storage("Cannot restrict the credentials file permissions.");
    }
    output << document.dump(2) << '\n';
    output.flush();
    if (!output) {
      output.close();
      fs::remove(temporary, ignored);
      return storage("Cannot write the server credentials file.");
    }
  }
  std::error_code error;
  fs::rename(temporary, target, error);
  if (error) {
    fs::remove(temporary, ignored);
    return storage("Cannot install the server credentials file.");
  }
  fs::permissions(target, fs::perms::owner_read | fs::perms::owner_write,
                  fs::perm_options::replace, ignored);
  return {};
}

AuthState::AuthState(SessionPolicy sessions, LoginLimitPolicy limits)
    : sessions_(sessions), limits_(limits) {
  totalFailures_.firstFailure = Clock::now();
  totalFailures_.lastFailure = Clock::now();
}

void AuthState::setCredentials(Credentials credentials) {
  std::lock_guard lock(mutex_);
  credentials_ = std::move(credentials);
  // Changing the credentials invalidates every existing session.
  activeSessions_.clear();
}

bool AuthState::configured() const {
  std::lock_guard lock(mutex_);
  return credentials_.has_value();
}

std::string AuthState::username() const {
  std::lock_guard lock(mutex_);
  return credentials_ ? credentials_->username : std::string{};
}

AuthState::Clock::time_point AuthState::now() const {
  return Clock::now() + testOffset_;
}

void AuthState::advanceClockForTests(std::chrono::seconds amount) {
  std::lock_guard lock(mutex_);
  testOffset_ += amount;
}

void AuthState::expireSessions(Clock::time_point moment) {
  for (auto it = activeSessions_.begin(); it != activeSessions_.end();) {
    const bool idle = moment - it->second.lastSeen > sessions_.idleTimeout;
    const bool old = moment - it->second.created > sessions_.absoluteLifetime;
    if (idle || old)
      it = activeSessions_.erase(it);
    else
      ++it;
  }
}

bool AuthState::limited(const std::string &clientKey, Clock::time_point moment,
                        int &retryAfterSeconds) {
  const auto remaining = [&](const FailureCounter &counter) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        moment - counter.firstFailure);
    const auto left = limits_.window - elapsed;
    return static_cast<int>(std::max<std::int64_t>(1, left.count()));
  };
  if (limits_.maxFailuresTotal > 0 &&
      totalFailures_.failures >= limits_.maxFailuresTotal) {
    if (moment - totalFailures_.firstFailure < limits_.window) {
      retryAfterSeconds = remaining(totalFailures_);
      return true;
    }
    totalFailures_ = {};
  }
  const auto found = failures_.find(clientKey);
  if (found == failures_.end())
    return false;
  if (moment - found->second.firstFailure >= limits_.window) {
    failures_.erase(found);
    return false;
  }
  if (found->second.failures < limits_.maxFailuresPerClient)
    return false;
  retryAfterSeconds = remaining(found->second);
  return true;
}

void AuthState::recordFailure(const std::string &clientKey,
                              Clock::time_point moment) {
  if (totalFailures_.failures == 0 ||
      moment - totalFailures_.firstFailure >= limits_.window) {
    totalFailures_.failures = 0;
    totalFailures_.firstFailure = moment;
  }
  ++totalFailures_.failures;
  totalFailures_.lastFailure = moment;

  auto found = failures_.find(clientKey);
  if (found == failures_.end()) {
    if (failures_.size() >= limits_.maxTrackedClients) {
      // Drop expired entries first, then the least recently active one, so the
      // table can never grow without bound.
      for (auto it = failures_.begin(); it != failures_.end();) {
        if (moment - it->second.firstFailure >= limits_.window)
          it = failures_.erase(it);
        else
          ++it;
      }
      if (failures_.size() >= limits_.maxTrackedClients) {
        auto oldest = std::min_element(
            failures_.begin(), failures_.end(),
            [](const auto &left, const auto &right) {
              return left.second.lastFailure < right.second.lastFailure;
            });
        if (oldest != failures_.end())
          failures_.erase(oldest);
      }
    }
    found = failures_.emplace(clientKey, FailureCounter{0, moment, moment}).first;
  }
  if (moment - found->second.firstFailure >= limits_.window) {
    found->second.failures = 0;
    found->second.firstFailure = moment;
  }
  ++found->second.failures;
  found->second.lastFailure = moment;
}

void AuthState::clearFailures(const std::string &clientKey) {
  failures_.erase(clientKey);
  totalFailures_ = {};
}

AuthState::LoginResult AuthState::login(const std::string &clientKey,
                                        const std::string &username,
                                        const std::string &password) {
  std::lock_guard lock(mutex_);
  const auto moment = now();
  expireSessions(moment);
  LoginResult result;
  if (limited(clientKey, moment, result.retryAfterSeconds)) {
    result.status = LoginStatus::RateLimited;
    return result;
  }
  if (!credentials_) {
    // Counted as a failure so that probing an unconfigured server is also
    // rate limited, and reported like any other rejected login.
    recordFailure(clientKey, moment);
    result.status = LoginStatus::InvalidCredentials;
    return result;
  }
  // The hash is always computed, so a wrong user name costs the same as a
  // wrong password.
  const bool nameMatches = constantTimeEquals(credentials_->username, username);
  auto verified = verifyPassword(credentials_->password, password);
  if (!verified) {
    result.status = LoginStatus::Unavailable;
    return result;
  }
  if (!nameMatches || !*verified) {
    recordFailure(clientKey, moment);
    result.status = LoginStatus::InvalidCredentials;
    return result;
  }
  auto token = randomToken();
  if (!token) {
    result.status = LoginStatus::Unavailable;
    return result;
  }
  if (activeSessions_.size() >= sessions_.maxSessions) {
    auto oldest = std::min_element(activeSessions_.begin(),
                                  activeSessions_.end(),
                                  [](const auto &left, const auto &right) {
                                    return left.second.lastSeen <
                                           right.second.lastSeen;
                                  });
    if (oldest != activeSessions_.end())
      activeSessions_.erase(oldest);
  }
  activeSessions_.emplace(sha256Hex(*token),
                          Session{credentials_->username, moment, moment});
  clearFailures(clientKey);
  result.status = LoginStatus::Ok;
  result.token = std::move(*token);
  return result;
}

std::optional<std::string> AuthState::authenticate(const std::string &token) {
  if (token.empty())
    return std::nullopt;
  std::lock_guard lock(mutex_);
  const auto moment = now();
  expireSessions(moment);
  const auto found = activeSessions_.find(sha256Hex(token));
  if (found == activeSessions_.end())
    return std::nullopt;
  found->second.lastSeen = moment;
  return found->second.username;
}

bool AuthState::logout(const std::string &token) {
  if (token.empty())
    return false;
  std::lock_guard lock(mutex_);
  return activeSessions_.erase(sha256Hex(token)) != 0;
}

std::size_t AuthState::sessionCount() const {
  std::lock_guard lock(mutex_);
  return activeSessions_.size();
}
} // namespace lexicon::http
