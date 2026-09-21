#include "ServerConfig.h"

#include "Utf8Path.h"

#include <charconv>
#include <filesystem>

namespace lexicon::http {
namespace {
namespace fs = std::filesystem;

std::unexpected<Error> invalid(std::string message) {
  return std::unexpected(Error{Error::Code::Validation, std::move(message)});
}

Result<long long> wholeNumber(const std::string &option,
                              const std::string &value, long long low,
                              long long high) {
  long long number = 0;
  const auto [end, error] =
      std::from_chars(value.data(), value.data() + value.size(), number);
  if (error != std::errc{} || end != value.data() + value.size())
    return invalid(option + " expects a whole number, got '" + value + "'.");
  if (number < low || number > high)
    return invalid(option + " must be between " + std::to_string(low) +
                   " and " + std::to_string(high) + ".");
  return number;
}

bool looksLikeOrigin(const std::string &origin) {
  // Exactly scheme "://" host [":" port] and nothing else: no path, no query,
  // no trailing slash, no credentials, no wildcard.
  const auto separator = origin.find("://");
  if (separator == std::string::npos)
    return false;
  const auto scheme = origin.substr(0, separator);
  if (scheme != "http" && scheme != "https")
    return false;
  const auto authority = origin.substr(separator + 3);
  if (authority.empty() || authority.find_first_of("/?#@*\\ \t") !=
                               std::string::npos)
    return false;
  const auto colon = authority.rfind(':');
  if (colon != std::string::npos) {
    const auto port = authority.substr(colon + 1);
    if (port.empty() ||
        port.find_first_not_of("0123456789") != std::string::npos)
      return false;
    return colon != 0;
  }
  return true;
}
} // namespace

std::string ServerConfig::resolvedAuthFilePath() const {
  if (!authFilePath.empty())
    return authFilePath;
  const auto database = utf8Path(databasePath);
  const auto directory = database.parent_path();
  return pathToUtf8(directory.empty() ? fs::path("lexicon-auth.json")
                                  : directory / "lexicon-auth.json");
}

bool isLoopbackAddress(const std::string &address) {
  if (address == "localhost" || address == "::1" || address == "[::1]")
    return true;
  return address.rfind("127.", 0) == 0;
}

Result<void> validate(const ServerConfig &config) {
  if (config.databasePath.empty())
    return invalid("--database cannot be empty.");
  // Port 0 asks the operating system for a free port; the CLI never sets it.
  if (config.port < 0 || config.port > 65535)
    return invalid("--port must be between 1 and 65535.");
  if (config.tlsCertificatePath.empty() != config.tlsPrivateKeyPath.empty())
    return invalid("--tls-cert and --tls-key must be used together.");
  if (!config.tlsEnabled() && !isLoopbackAddress(config.listenAddress) &&
      !config.allowInsecureHttp)
    return invalid(
        "Refusing to serve password authentication over plaintext HTTP on " +
        config.listenAddress +
        ". Configure --tls-cert and --tls-key, bind to 127.0.0.1 behind a "
        "reverse proxy, or pass --allow-insecure-http to override.");
  if (config.maxJsonBytes < 1024)
    return invalid("--max-json-bytes must be at least 1024.");
  if (config.maxBlobBytes < config.maxJsonBytes)
    return invalid("--max-blob-bytes must not be smaller than "
                   "--max-json-bytes.");
  if (config.sessions.idleTimeout.count() <= 0 ||
      config.sessions.absoluteLifetime.count() <= 0)
    return invalid("Session timeouts must be positive.");
  if (config.sessions.absoluteLifetime < config.sessions.idleTimeout)
    return invalid("--session-max-lifetime must not be shorter than "
                   "--session-idle-timeout.");
  if (config.sessions.maxSessions == 0)
    return invalid("--max-sessions must be at least 1.");
  if (config.loginLimits.maxConcurrentHashes < 1 ||
      config.loginLimits.maxConcurrentHashes > AuthState::maxHashSlots)
    return invalid("--login-max-parallel-hashes must be between 1 and " +
                   std::to_string(AuthState::maxHashSlots) + ".");
  for (const auto &origin : config.allowedOrigins)
    if (!looksLikeOrigin(origin))
      return invalid("--allowed-origin expects an exact origin such as "
                     "https://lexicon.example.com, got '" + origin + "'.");
  return {};
}

std::string usageText() {
  return R"(LexiconServer - REST/JSON API for Lexicon.

Usage:
  LexiconServer [serve] [options]
  LexiconServer auth set-user [options]
  LexiconServer auth show [options]
  LexiconServer --help | --version

The server exposes JSON under /api/v1 only. It never serves HTML, CSS,
JavaScript or any other web asset; deploy lexicon-web on a static host.

Options:
  --database PATH            SQLite database file (default: lexicon.db)
  --auth-file PATH           Credentials file
                             (default: <database directory>/lexicon-auth.json)
  --listen ADDRESS           Bind address (default: 127.0.0.1)
  --port PORT                TCP port (default: 8628)
  --tls-cert PATH            PEM certificate chain for embedded HTTPS
  --tls-key PATH             PEM private key for embedded HTTPS
  --allow-insecure-http      Permit a non-loopback plaintext listener
  --allowed-origin ORIGIN    Exact CORS origin, repeatable
  --trusted-proxy ADDRESS    Honour X-Forwarded-For from this peer, repeatable
  --session-idle-timeout S   Idle session timeout in seconds (default: 28800)
  --session-max-lifetime S   Absolute session lifetime in seconds
                             (default: 604800)
  --max-sessions N           Concurrent sessions kept in memory (default: 32)
  --max-json-bytes N         Maximum JSON request body (default: 1048576)
  --max-blob-bytes N         Maximum blob upload (default: 67108864)
  --read-timeout S           Socket read timeout in seconds (default: 15)
  --write-timeout S          Socket write timeout in seconds (default: 15)
  --keep-alive-timeout S     Keep-alive timeout in seconds (default: 5)
  --login-max-failures N     Failed logins per client before HTTP 429
                             (default: 10)
  --login-failure-window S   Rate limit window in seconds (default: 900)
  --login-max-failures-total N
                             Failed logins from all clients before HTTP 429
                             (default: 200, 0 disables)
  --login-max-parallel-hashes N
                             Password derivations allowed to run at once
                             (default: 2)
  --quiet                    Do not write a request log line per request
  -h, --help                 Show this help
  --version                  Show the version
)";
}

Result<CommandLine> parseCommandLine(const std::vector<std::string> &arguments) {
  CommandLine parsed;
  std::size_t index = 0;
  if (index < arguments.size() && !arguments[index].starts_with("-")) {
    const auto &command = arguments[index];
    if (command == "serve") {
      ++index;
    } else if (command == "auth") {
      ++index;
      if (index >= arguments.size())
        return invalid("auth needs a subcommand: set-user or show.");
      const auto &subcommand = arguments[index++];
      if (subcommand == "set-user")
        parsed.command = Command::AuthSetUser;
      else if (subcommand == "show")
        parsed.command = Command::AuthShow;
      else
        return invalid("Unknown auth subcommand '" + subcommand + "'.");
    } else {
      return invalid("Unknown command '" + command + "'.");
    }
  }

  const auto next = [&](const std::string &option) -> Result<std::string> {
    if (index + 1 >= arguments.size())
      return invalid(option + " needs a value.");
    return arguments[++index];
  };

  for (; index < arguments.size(); ++index) {
    const std::string &option = arguments[index];
    if (option == "-h" || option == "--help") {
      parsed.command = Command::Help;
      return parsed;
    }
    if (option == "--version") {
      parsed.command = Command::Version;
      return parsed;
    }
    if (option == "--allow-insecure-http") {
      parsed.config.allowInsecureHttp = true;
      continue;
    }
    if (option == "--quiet") {
      parsed.config.requestLogging = false;
      continue;
    }
    auto value = next(option);
    if (!value)
      return std::unexpected(value.error());
    if (option == "--database")
      parsed.config.databasePath = *value;
    else if (option == "--auth-file")
      parsed.config.authFilePath = *value;
    else if (option == "--listen")
      parsed.config.listenAddress = *value;
    else if (option == "--tls-cert")
      parsed.config.tlsCertificatePath = *value;
    else if (option == "--tls-key")
      parsed.config.tlsPrivateKeyPath = *value;
    else if (option == "--allowed-origin")
      parsed.config.allowedOrigins.push_back(*value);
    else if (option == "--trusted-proxy")
      parsed.config.trustedProxies.push_back(*value);
    else {
      auto number = [&](long long low, long long high) {
        return wholeNumber(option, *value, low, high);
      };
      if (option == "--port") {
        auto port = number(1, 65535);
        if (!port)
          return std::unexpected(port.error());
        parsed.config.port = static_cast<int>(*port);
      } else if (option == "--session-idle-timeout") {
        auto seconds = number(1, 365LL * 24 * 60 * 60);
        if (!seconds)
          return std::unexpected(seconds.error());
        parsed.config.sessions.idleTimeout = std::chrono::seconds(*seconds);
      } else if (option == "--session-max-lifetime") {
        auto seconds = number(1, 365LL * 24 * 60 * 60);
        if (!seconds)
          return std::unexpected(seconds.error());
        parsed.config.sessions.absoluteLifetime = std::chrono::seconds(*seconds);
      } else if (option == "--max-sessions") {
        auto count = number(1, 10000);
        if (!count)
          return std::unexpected(count.error());
        parsed.config.sessions.maxSessions = static_cast<std::size_t>(*count);
      } else if (option == "--max-json-bytes") {
        auto bytes = number(1024, 64LL * 1024 * 1024);
        if (!bytes)
          return std::unexpected(bytes.error());
        parsed.config.maxJsonBytes = static_cast<std::size_t>(*bytes);
      } else if (option == "--max-blob-bytes") {
        auto bytes = number(1024, 4096LL * 1024 * 1024);
        if (!bytes)
          return std::unexpected(bytes.error());
        parsed.config.maxBlobBytes = static_cast<std::size_t>(*bytes);
      } else if (option == "--read-timeout") {
        auto seconds = number(1, 3600);
        if (!seconds)
          return std::unexpected(seconds.error());
        parsed.config.readTimeoutSeconds = static_cast<int>(*seconds);
      } else if (option == "--write-timeout") {
        auto seconds = number(1, 3600);
        if (!seconds)
          return std::unexpected(seconds.error());
        parsed.config.writeTimeoutSeconds = static_cast<int>(*seconds);
      } else if (option == "--keep-alive-timeout") {
        auto seconds = number(1, 3600);
        if (!seconds)
          return std::unexpected(seconds.error());
        parsed.config.keepAliveTimeoutSeconds = static_cast<int>(*seconds);
      } else if (option == "--login-max-failures") {
        auto count = number(1, 100000);
        if (!count)
          return std::unexpected(count.error());
        parsed.config.loginLimits.maxFailuresPerClient =
            static_cast<int>(*count);
      } else if (option == "--login-failure-window") {
        auto seconds = number(1, 24 * 60 * 60);
        if (!seconds)
          return std::unexpected(seconds.error());
        parsed.config.loginLimits.window = std::chrono::seconds(*seconds);
      } else if (option == "--login-max-failures-total") {
        auto count = number(0, 1000000);
        if (!count)
          return std::unexpected(count.error());
        parsed.config.loginLimits.maxFailuresTotal = static_cast<int>(*count);
      } else if (option == "--login-max-parallel-hashes") {
        auto count = number(1, AuthState::maxHashSlots);
        if (!count)
          return std::unexpected(count.error());
        parsed.config.loginLimits.maxConcurrentHashes = static_cast<int>(*count);
      } else {
        return invalid("Unknown option '" + option + "'.");
      }
    }
  }
  return parsed;
}
} // namespace lexicon::http
