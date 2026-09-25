#include "AuthState.h"

#include "Utf8Path.h"
#include "TempFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <system_error>

namespace lexicon::http {
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;

std::unexpected<Error> invalid(std::string message) {
  return std::unexpected(Error{Error::Code::Validation, std::move(message)});
}
std::uint64_t positiveNumber(const Json &json, const char *key,
                             std::uint64_t fallback) {
  const auto found = json.find(key);
  if (found == json.end() || !found->is_number_unsigned())
    return fallback;
  return found->get<std::uint64_t>();
}

// Replaces the file at `path` with `text`, readable by this account only. The
// write goes through an exclusively created temporary file with an
// unpredictable name and is installed atomically, so a crash or a concurrent
// reader sees the old document or the new one, never half of one, and no
// attacker can pre-create the temporary path.
Result<void> writePrivateFile(const std::string &path, const std::string &text) {
  const auto target = utf8Path(path);
  const auto directory = target.parent_path();
  auto staging = TempFile::create(directory.empty() ? std::string(".")
                                                    : pathToUtf8(directory));
  if (!staging)
    return std::unexpected(staging.error());
  if (auto written = staging->write(text.data(), text.size()); !written)
    return written;
  return staging->replace(path, TempFile::Protection::OwnerOnly);
}

bool validTokenHash(const std::string &hash) {
  return hash.size() == 64 && std::all_of(hash.begin(), hash.end(), [](char ch) {
           return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
         });
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
  // Changing the password rewrites an existing file.
  return writePrivateFile(path, document.dump(2) + "\n");
}

bool containsNul(std::string_view text) {
  return text.find('\0') != std::string_view::npos;
}

namespace {
std::ptrdiff_t hashSlotCount(const LoginLimitPolicy &limits) {
  return std::clamp<std::ptrdiff_t>(limits.maxConcurrentHashes, 1,
                                    AuthState::maxHashSlots);
}
} // namespace

AuthState::AuthState(SessionPolicy sessions, LoginLimitPolicy limits)
    : sessions_(sessions), limits_(limits), hashSlots_(hashSlotCount(limits)) {
  totalFailures_.firstFailure = Clock::now();
  totalFailures_.lastFailure = Clock::now();
}

AuthState::HashPermit::HashPermit(AuthState &state) : state_(state) {
  state_.hashSlots_.acquire();
  const int active = state_.activeHashes_.fetch_add(1) + 1;
  int peak = state_.peakHashes_.load(std::memory_order_relaxed);
  while (active > peak &&
         !state_.peakHashes_.compare_exchange_weak(peak, active))
    ;
}

AuthState::HashPermit::~HashPermit() {
  state_.activeHashes_.fetch_sub(1);
  state_.hashSlots_.release();
}

int AuthState::peakPasswordHashConcurrency() const {
  return peakHashes_.load();
}

void AuthState::setCredentials(Credentials credentials) {
  std::unique_lock lock(mutex_);
  credentials_ = std::move(credentials);
  // Changing the credentials invalidates every existing session.
  activeSessions_.clear();
  rememberedDevices_.clear();
  saveSessions(lock);
}

std::string AuthState::credentialsFingerprint() const {
  if (!credentials_)
    return {};
  return sha256Hex(credentials_->username + '\n' +
                   base64Encode(credentials_->password.salt) + '\n' +
                   base64Encode(credentials_->password.hash));
}

namespace {
long long epochSeconds(std::chrono::system_clock::time_point moment) {
  return std::chrono::duration_cast<std::chrono::seconds>(moment.time_since_epoch()).count();
}
std::chrono::system_clock::time_point fromEpochSeconds(long long seconds) {
  return std::chrono::system_clock::time_point(std::chrono::seconds(seconds));
}
} // namespace

Result<std::size_t> AuthState::useSessionFile(const std::string &path) {
  std::unique_lock lock(mutex_);
  sessionFile_ = path;
  std::ifstream input(utf8Path(path), std::ios::binary);
  if (!input)
    return std::size_t{0}; // Nobody has signed in yet.
  Json document;
  try {
    input >> document;
  } catch (const std::exception &) {
    return invalid("The session file is not valid JSON. It is replaced at the next sign-in.");
  }
  if (!document.is_object() || positiveNumber(document, "version", 0) != 1 ||
      !document.contains("sessions") || !document["sessions"].is_array())
    return invalid("Unsupported session file. It is replaced at the next sign-in.");
  // Sessions opened with other credentials - before `auth set-user` changed
  // the password, say - end here.
  const auto credentials = document.find("credentials");
  if (credentials == document.end() || !credentials->is_string() ||
      credentials->get<std::string>() != credentialsFingerprint() || !credentials_)
    return std::size_t{0};
  const auto moment = now();
  std::map<std::string, Session> restored;
  for (const auto &entry : document["sessions"]) {
    if (!entry.is_object()) continue;
    const auto hash = entry.value("tokenHash", std::string{});
    const auto user = entry.value("username", std::string{});
    const auto created = entry.find("created");
    const auto lastSeen = entry.find("lastSeen");
    if (!validTokenHash(hash) || user != credentials_->username ||
        created == entry.end() || !created->is_number_integer() ||
        lastSeen == entry.end() || !lastSeen->is_number_integer())
      continue;
    Session session{user, fromEpochSeconds(created->get<long long>()),
                    fromEpochSeconds(lastSeen->get<long long>()),
                    fromEpochSeconds(lastSeen->get<long long>()),
                    entry.value("deviceId", std::string{})};
    if (moment - session.lastSeen > sessions_.idleTimeout ||
        moment - session.created > sessions_.absoluteLifetime)
      continue;
    restored.emplace(hash, std::move(session));
  }
  // The most recently used ones, if the file holds more than are allowed.
  while (restored.size() > sessions_.maxSessions)
    restored.erase(std::min_element(restored.begin(), restored.end(),
                                    [](const auto &left, const auto &right) {
                                      return left.second.lastSeen < right.second.lastSeen;
                                    }));
  activeSessions_ = std::move(restored);
  rememberedDevices_.clear();
  if (document.contains("devices") && document["devices"].is_array()) {
    for (const auto &entry : document["devices"]) {
      if (!entry.is_object()) continue;
      const auto hash = entry.value("tokenHash", std::string{});
      const auto id = entry.value("id", std::string{});
      const auto user = entry.value("username", std::string{});
      const auto previous = entry.value("previousHash", std::string{});
      const auto created = entry.find("created");
      const auto lastUsed = entry.find("lastUsed");
      if (!validTokenHash(hash) || id.empty() || user != credentials_->username ||
          (!previous.empty() && !validTokenHash(previous)) ||
          created == entry.end() || !created->is_number_integer() ||
          lastUsed == entry.end() || !lastUsed->is_number_integer()) continue;
      const auto used = fromEpochSeconds(lastUsed->get<long long>());
      if (moment - used > std::chrono::hours(24 * 90)) continue;
      rememberedDevices_.emplace(hash, RememberedDevice{id, user,
          fromEpochSeconds(created->get<long long>()), used, previous});
    }
  }
  while (rememberedDevices_.size() > sessions_.maxSessions) {
    auto oldest = std::min_element(rememberedDevices_.begin(), rememberedDevices_.end(),
        [](const auto &left, const auto &right) {
          return left.second.lastUsed < right.second.lastUsed;
        });
    rememberedDevices_.erase(oldest);
  }
  return activeSessions_.size();
}

std::string AuthState::sessionDocument() {
  Json sessions = Json::array();
  for (auto &[hash, session] : activeSessions_) {
    session.savedLastSeen = session.lastSeen;
    sessions.push_back(Json{{"tokenHash", hash}, {"username", session.username},
                            {"created", epochSeconds(session.created)},
                            {"lastSeen", epochSeconds(session.lastSeen)},
                            {"deviceId", session.deviceId}});
  }
  Json devices = Json::array();
  for (const auto &[hash, device] : rememberedDevices_)
    devices.push_back(Json{{"tokenHash", hash}, {"id", device.id},
                           {"username", device.username},
                           {"created", epochSeconds(device.created)},
                           {"lastUsed", epochSeconds(device.lastUsed)},
                           {"previousHash", device.previousHash}});
  return Json{{"version", 1}, {"credentials", credentialsFingerprint()},
              {"sessions", std::move(sessions)}, {"devices", std::move(devices)}}.dump(2) + "\n";
}

Result<void> AuthState::persistLocked() {
  if (sessionFile_.empty())
    return invalid("Remembered devices require a session file.");
  const auto text = sessionDocument();
  const auto generation = ++saveGeneration_;
  std::lock_guard save(saveMutex_);
  if (auto written = writePrivateFile(sessionFile_, text); !written) return written;
  savedGeneration_ = generation;
  return {};
}

void AuthState::saveSessions(std::unique_lock<std::mutex> &lock) {
  if (sessionFile_.empty())
    return;
  const auto text = sessionDocument();
  const auto generation = ++saveGeneration_;
  const auto path = sessionFile_;
  lock.unlock();
  std::lock_guard save(saveMutex_);
  // A newer snapshot may have been written while this one waited.
  if (generation <= savedGeneration_)
    return;
  if (auto written = writePrivateFile(path, text); !written)
    std::cerr << "lexicon-http: cannot save the sessions to " << path << ": "
              << written.error().message << '\n';
  savedGeneration_ = generation;
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

bool AuthState::expireSessions(Clock::time_point moment) {
  bool expired = false;
  for (auto it = activeSessions_.begin(); it != activeSessions_.end();) {
    const bool idle = moment - it->second.lastSeen > sessions_.idleTimeout;
    const bool old = moment - it->second.created > sessions_.absoluteLifetime;
    if (idle || old) {
      it = activeSessions_.erase(it);
      expired = true;
    } else {
      ++it;
    }
  }
  return expired;
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
                                        const std::string &password,
                                        bool rememberDevice) {
  LoginResult result;

  // Phase one, under the lock: apply the rate limit and take a copy of the
  // credentials to verify against.
  std::optional<Credentials> expected;
  {
    std::lock_guard lock(mutex_);
    const auto moment = now();
    expireSessions(moment);
    if (limited(clientKey, moment, result.retryAfterSeconds)) {
      result.status = LoginStatus::RateLimited;
      return result;
    }
    // scrypt keys HMAC with the password, and HMAC pads a key shorter than
    // its block with zero bytes, so "secret" and "secret\0" derive the same
    // key. No typed password contains a NUL, so one is refused here rather
    // than silently accepted as equivalent.
    if (containsNul(username) || containsNul(password)) {
      recordFailure(clientKey, moment);
      result.status = LoginStatus::InvalidCredentials;
      return result;
    }
    expected = credentials_;
  }

  // Phase two, with no lock held: derive the key. This is deliberately slow,
  // which is exactly why every other request must not wait behind it. At most
  // maxConcurrentHashes derivations run at a time.
  bool credentialsMatch = false;
  if (expected) {
    const HashPermit permit(*this);
    // The hash is always computed, so a wrong user name costs the same as a
    // wrong password.
    const bool nameMatches = constantTimeEquals(expected->username, username);
    auto verified = verifyPassword(expected->password, password);
    if (!verified) {
      result.status = LoginStatus::Unavailable;
      return result;
    }
    credentialsMatch = nameMatches && *verified;
  }

  // Phase three, under the lock again: record the outcome.
  std::unique_lock lock(mutex_);
  const auto moment = now();
  // A password change during the derivation invalidates what was verified.
  const bool stillCurrent =
      expected && credentials_ &&
      credentials_->username == expected->username &&
      credentials_->password.hash == expected->password.hash;
  if (!credentialsMatch || !stillCurrent) {
    // Probing an unconfigured or just-changed server is rate limited and
    // reported exactly like any other rejected login.
    recordFailure(clientKey, moment);
    result.status = LoginStatus::InvalidCredentials;
    return result;
  }
  auto token = randomToken();
  if (!token) {
    result.status = LoginStatus::Unavailable;
    return result;
  }
  std::string refreshToken;
  std::string deviceId;
  if (rememberDevice) {
    if (sessionFile_.empty()) {
      result.status = LoginStatus::CannotRemember;
      return result;
    }
    auto secret = randomToken();
    auto id = randomToken();
    if (!secret || !id) {
      result.status = LoginStatus::Unavailable;
      return result;
    }
    refreshToken = std::move(*secret);
    deviceId = std::move(*id);
  }
  const auto oldSessions = rememberDevice ? activeSessions_ : decltype(activeSessions_){};
  const auto oldDevices = rememberDevice ? rememberedDevices_ : decltype(rememberedDevices_){};
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
                          Session{credentials_->username, moment, moment, moment, deviceId});
  if (rememberDevice) {
    if (rememberedDevices_.size() >= sessions_.maxSessions) {
      auto oldest = std::min_element(rememberedDevices_.begin(), rememberedDevices_.end(),
          [](const auto &left, const auto &right) {
            return left.second.lastUsed < right.second.lastUsed;
          });
      if (oldest != rememberedDevices_.end()) {
        revokeDeviceSessions(oldest->second.id);
        rememberedDevices_.erase(oldest);
      }
    }
    rememberedDevices_.emplace(sha256Hex(refreshToken),
        RememberedDevice{deviceId, credentials_->username, moment, moment, {}});
    if (auto saved = persistLocked(); !saved) {
      activeSessions_ = oldSessions;
      rememberedDevices_ = oldDevices;
      result.status = LoginStatus::Unavailable;
      return result;
    }
  }
  clearFailures(clientKey);
  result.status = LoginStatus::Ok;
  result.token = std::move(*token);
  result.refreshToken = std::move(refreshToken);
  if (!rememberDevice) saveSessions(lock);
  return result;
}

void AuthState::revokeDeviceSessions(const std::string &id) {
  for (auto it = activeSessions_.begin(); it != activeSessions_.end();) {
    if (it->second.deviceId == id) it = activeSessions_.erase(it);
    else ++it;
  }
}

AuthState::LoginResult AuthState::refresh(const std::string &refreshToken) {
  LoginResult result;
  if (refreshToken.empty()) return result;
  std::unique_lock lock(mutex_);
  const auto hash = sha256Hex(refreshToken);
  const auto moment = now();
  auto found = rememberedDevices_.find(hash);
  if (found == rememberedDevices_.end()) {
    // A reused, already rotated secret ends the whole device grant.
    for (auto it = rememberedDevices_.begin(); it != rememberedDevices_.end(); ++it) {
      if (it->second.previousHash == hash) {
        revokeDeviceSessions(it->second.id);
        rememberedDevices_.erase(it);
        saveSessions(lock);
        break;
      }
    }
    return result;
  }
  if (moment - found->second.lastUsed > std::chrono::hours(24 * 90)) {
    revokeDeviceSessions(found->second.id);
    rememberedDevices_.erase(found);
    saveSessions(lock);
    return result;
  }
  auto access = randomToken();
  auto nextSecret = randomToken();
  if (!access || !nextSecret) {
    result.status = LoginStatus::Unavailable;
    return result;
  }
  const auto oldSessions = activeSessions_;
  const auto oldDevices = rememberedDevices_;
  RememberedDevice device = found->second;
  rememberedDevices_.erase(found);
  device.previousHash = hash;
  device.lastUsed = moment;
  rememberedDevices_.emplace(sha256Hex(*nextSecret), device);
  revokeDeviceSessions(device.id);
  if (activeSessions_.size() >= sessions_.maxSessions) {
    auto oldest = std::min_element(activeSessions_.begin(), activeSessions_.end(),
        [](const auto &left, const auto &right) {
          return left.second.lastSeen < right.second.lastSeen;
        });
    if (oldest != activeSessions_.end()) activeSessions_.erase(oldest);
  }
  activeSessions_.emplace(sha256Hex(*access),
      Session{device.username, moment, moment, moment, device.id});
  if (auto saved = persistLocked(); !saved) {
    activeSessions_ = oldSessions;
    rememberedDevices_ = oldDevices;
    result.status = LoginStatus::Unavailable;
    return result;
  }
  result.status = LoginStatus::Ok;
  result.token = std::move(*access);
  result.refreshToken = std::move(*nextSecret);
  return result;
}

void AuthState::forgetDevice(const std::string &refreshToken) {
  if (refreshToken.empty()) return;
  std::unique_lock lock(mutex_);
  const auto hash = sha256Hex(refreshToken);
  for (auto it = rememberedDevices_.begin(); it != rememberedDevices_.end(); ++it) {
    if (it->first == hash || it->second.previousHash == hash) {
      revokeDeviceSessions(it->second.id);
      rememberedDevices_.erase(it);
      saveSessions(lock);
      return;
    }
  }
}

std::vector<AuthState::DeviceView> AuthState::listDevices(const std::string &currentToken) const {
  std::lock_guard lock(mutex_);
  const auto session = activeSessions_.find(sha256Hex(currentToken));
  if (session == activeSessions_.end()) return {};
  std::vector<DeviceView> result;
  for (const auto &[hash, device] : rememberedDevices_)
    if (now() - device.lastUsed <= std::chrono::hours(24 * 90))
      result.push_back({device.id, epochSeconds(device.created),
                        epochSeconds(device.lastUsed), device.id == session->second.deviceId});
  std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
    return a.lastUsedSeconds > b.lastUsedSeconds;
  });
  return result;
}

bool AuthState::revokeDevice(const std::string &currentToken, const std::string &id) {
  std::unique_lock lock(mutex_);
  if (!activeSessions_.contains(sha256Hex(currentToken))) return false;
  for (auto it = rememberedDevices_.begin(); it != rememberedDevices_.end(); ++it) {
    if (it->second.id == id) {
      revokeDeviceSessions(id);
      rememberedDevices_.erase(it);
      saveSessions(lock);
      return true;
    }
  }
  return false;
}

std::optional<std::string> AuthState::authenticate(const std::string &token) {
  if (token.empty())
    return std::nullopt;
  std::unique_lock lock(mutex_);
  const auto moment = now();
  const bool expired = expireSessions(moment);
  const auto found = activeSessions_.find(sha256Hex(token));
  if (found == activeSessions_.end()) {
    if (expired)
      saveSessions(lock);
    return std::nullopt;
  }
  found->second.lastSeen = moment;
  auto user = found->second.username;
  // The idle timer survives a restart to within a minute, without a write
  // for every request.
  if (expired || moment - found->second.savedLastSeen >= std::chrono::minutes(1))
    saveSessions(lock);
  return user;
}

bool AuthState::logout(const std::string &token) {
  if (token.empty())
    return false;
  std::unique_lock lock(mutex_);
  const auto found = activeSessions_.find(sha256Hex(token));
  if (found == activeSessions_.end()) return false;
  const auto deviceId = found->second.deviceId;
  activeSessions_.erase(found);
  if (!deviceId.empty()) {
    for (auto it = rememberedDevices_.begin(); it != rememberedDevices_.end(); ++it)
      if (it->second.id == deviceId) { rememberedDevices_.erase(it); break; }
    revokeDeviceSessions(deviceId);
  }
  saveSessions(lock);
  return true;
}

std::size_t AuthState::sessionCount() const {
  std::lock_guard lock(mutex_);
  return activeSessions_.size();
}

std::vector<AuthState::SessionView> AuthState::listSessions(const std::string &currentToken) const {
  const auto currentHash = sha256Hex(currentToken);
  std::lock_guard lock(mutex_);
  if (activeSessions_.find(currentHash) == activeSessions_.end()) return {};
  std::vector<SessionView> result;
  result.reserve(activeSessions_.size());
  for (const auto &[hash, session] : activeSessions_)
    result.push_back({hash.substr(0, 24), epochSeconds(session.created),
                      epochSeconds(session.lastSeen), hash == currentHash});
  std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
    return a.createdAtSeconds > b.createdAtSeconds;
  });
  return result;
}

bool AuthState::revokeSession(const std::string &currentToken, const std::string &sessionId) {
  if (sessionId.size() != 24 || sessionId.find_first_not_of("0123456789abcdef") != std::string::npos)
    return false;
  std::unique_lock lock(mutex_);
  if (!activeSessions_.contains(sha256Hex(currentToken))) return false;
  for (auto it = activeSessions_.begin(); it != activeSessions_.end(); ++it) {
    if (it->first.starts_with(sessionId)) {
      const auto deviceId = it->second.deviceId;
      activeSessions_.erase(it);
      if (!deviceId.empty()) {
        for (auto device = rememberedDevices_.begin(); device != rememberedDevices_.end(); ++device)
          if (device->second.id == deviceId) { rememberedDevices_.erase(device); break; }
        revokeDeviceSessions(deviceId);
      }
      saveSessions(lock);
      return true;
    }
  }
  return false;
}

Result<bool> AuthState::changePassword(const std::string &currentToken,
                                       const std::string &currentPassword,
                                       const std::string &newPassword,
                                       const std::string &authFilePath) {
  if (newPassword.size() < 12 || newPassword.size() > 1024 || containsNul(newPassword))
    return std::unexpected(Error{Error::Code::Validation,
                                 "Choose a new password of 12 to 1024 characters without NUL bytes."});
  if (currentPassword.size() > 1024 || containsNul(currentPassword)) return false;
  std::lock_guard changeLock(credentialChangeMutex_);
  if (!authenticate(currentToken)) return false;
  Credentials expected;
  {
    std::lock_guard lock(mutex_);
    if (!credentials_) return false;
    expected = *credentials_;
  }
  {
    const HashPermit permit(*this);
    auto verified = verifyPassword(expected.password, currentPassword);
    if (!verified) return std::unexpected(verified.error());
    if (!*verified) return false;
  }
  auto hashed = hashPassword(newPassword, expected.password.parameters);
  if (!hashed) return std::unexpected(hashed.error());
  auto onDisk = readCredentialsFile(authFilePath);
  if (!onDisk) return std::unexpected(onDisk.error());
  if (onDisk->username != expected.username || onDisk->password.hash != expected.password.hash ||
      onDisk->password.salt != expected.password.salt)
    return std::unexpected(Error{Error::Code::Conflict,
                                 "Credentials changed on disk. Restart the server before changing the password."});
  {
    std::lock_guard lock(mutex_);
    if (!activeSessions_.contains(sha256Hex(currentToken)) || !credentials_ ||
        credentials_->password.hash != expected.password.hash) return false;
  }
  Credentials replacement{expected.username, std::move(*hashed)};
  if (auto written = writeCredentialsFile(authFilePath, replacement); !written)
    return std::unexpected(written.error());
  setCredentials(std::move(replacement));
  return true;
}
} // namespace lexicon::http
