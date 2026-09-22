// Composition root for LexiconServer: SqliteRepository + LexiconApplication +
// HTTP adapter. No Qt, no HTML, no static files.
#include "AuthState.h"
#include "FilePath.h"
#include "LexiconApplication.h"
#include "RestServer.h"
#include "ServerConfig.h"
#include "SqliteRepository.h"

#include <atomic>
#include <csignal>
#include <iterator>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {
using lexicon::http::AuthState;
using lexicon::http::Command;
using lexicon::http::Credentials;
using lexicon::http::RestServer;
using lexicon::http::ScryptParameters;
using lexicon::http::ServerConfig;

constexpr const char *kVersion = "Lexicon REST server 0.1.0 (API v1)";

std::atomic<RestServer *> runningServer{nullptr};

void requestShutdown(int) {
  if (RestServer *server = runningServer.load())
    server->stop();
}

// Reads one line of input as UTF-8. With `echo` false the characters are not
// shown, so a password never appears on screen and never reaches the shell
// history or the process list.
//
// A Windows console does not hand over UTF-8: the narrow console input is the
// input code page, so a real console is read wide and converted here, the same
// way the command line is. Redirected input is defined to be UTF-8 already and
// is read as bytes, which is what a pipe or a file from any other tool gives.
bool readLine(const std::string &prompt, std::string &value, bool echo) {
  std::cout << prompt << std::flush;
  value.clear();
#ifdef _WIN32
  HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode = 0;
  const bool console = GetConsoleMode(input, &mode) != 0;
  if (!console) {
    const bool redirected = static_cast<bool>(std::getline(std::cin, value));
    if (!echo)
      std::cout << '\n';
    return redirected;
  }
  if (!echo)
    SetConsoleMode(input, mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));
  std::wstring line;
  bool ok = true;
  for (;;) {
    wchar_t buffer[256];
    DWORD read = 0;
    if (!ReadConsoleW(input, buffer, static_cast<DWORD>(std::size(buffer)),
                      &read, nullptr)) {
      ok = false;
      break;
    }
    if (read == 0) // End of input, for instance Ctrl+Z.
      break;
    line.append(buffer, read);
    if (line.find(L'\n') != std::wstring::npos)
      break;
  }
  if (!echo) {
    SetConsoleMode(input, mode);
    std::cout << '\n';
  }
  while (!line.empty() && (line.back() == L'\n' || line.back() == L'\r'))
    line.pop_back();
  if (!ok)
    return false;
  auto converted = lexicon::http::wideToUtf8(line);
  if (!converted) {
    std::cerr << converted.error().message << '\n';
    return false;
  }
  value = std::move(*converted);
  return true;
#else
  termios original{};
  const bool interactive = !echo && ::isatty(STDIN_FILENO) != 0 &&
                           ::tcgetattr(STDIN_FILENO, &original) == 0;
  if (interactive) {
    termios quiet = original;
    quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &quiet);
  }
  const bool ok = static_cast<bool>(std::getline(std::cin, value));
  if (interactive)
    ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
  if (!echo)
    std::cout << '\n';
  return ok;
#endif
}

int authSetUser(const ServerConfig &config) {
  const auto path = config.resolvedAuthFilePath();
  std::cout << "Configuring the Lexicon server user in " << path << ".\n";
  std::string username;
  if (!readLine("User name: ", username, true)) {
    std::cerr << "Aborted.\n";
    return 1;
  }
  username = lexicon::trim(username);
  if (username.empty() || lexicon::http::containsNul(username)) {
    std::cerr << "The user name cannot be empty or contain a NUL character.\n";
    return 1;
  }
  std::string password;
  std::string confirmation;
  if (!readLine("Password: ", password, false) ||
      !readLine("Repeat password: ", confirmation, false)) {
    std::cerr << "Aborted.\n";
    return 1;
  }
  if (password != confirmation) {
    std::cerr << "The passwords do not match.\n";
    return 1;
  }
  if (lexicon::http::containsNul(password)) {
    std::cerr << "The password cannot contain a NUL character.\n";
    return 1;
  }
  if (password.size() < 12) {
    std::cerr << "Choose a password of at least 12 characters.\n";
    return 1;
  }
  std::cout << "Hashing the password with scrypt. This takes a moment...\n";
  auto hashed =
      lexicon::http::hashPassword(password, ScryptParameters{});
  password.assign(password.size(), '\0');
  confirmation.assign(confirmation.size(), '\0');
  if (!hashed) {
    std::cerr << hashed.error().message << '\n';
    return 1;
  }
  Credentials credentials{username, *hashed};
  if (auto written = lexicon::http::writeCredentialsFile(path, credentials);
      !written) {
    std::cerr << written.error().message << '\n';
    return 1;
  }
  std::cout << "Saved. Existing sessions stop working when the server restarts.\n";
  return 0;
}

int authShow(const ServerConfig &config) {
  const auto path = config.resolvedAuthFilePath();
  auto credentials = lexicon::http::readCredentialsFile(path);
  if (!credentials) {
    std::cerr << credentials.error().message << '\n';
    return 1;
  }
  std::cout << "Credentials file: " << path << '\n'
            << "User name: " << credentials->username << '\n'
            << "Password hash: " << credentials->password.algorithm << " N="
            << credentials->password.parameters.n
            << " r=" << credentials->password.parameters.r
            << " p=" << credentials->password.parameters.p << '\n';
  return 0;
}

int serve(const ServerConfig &config) {
  if (auto valid = lexicon::http::validate(config); !valid) {
    std::cerr << valid.error().message << '\n';
    return 2;
  }
  auto credentials =
      lexicon::http::readCredentialsFile(config.resolvedAuthFilePath());
  if (!credentials) {
    std::cerr << "Cannot start: " << credentials.error().message
              << "\nRun 'LexiconServer auth set-user' on this machine first.\n";
    return 2;
  }

  SqliteRepository repository;
  if (auto opened = repository.open(config.databasePath); !opened) {
    std::cerr << "Cannot open the database: " << opened.error().message << '\n';
    return 1;
  }
  lexicon::LexiconApplication application(repository);
  AuthState auth(config.sessions, config.loginLimits);
  auth.setCredentials(*credentials);
  std::size_t restoredSessions = 0;
  if (config.persistSessions) {
    auto restored = auth.useSessionFile(config.resolvedSessionFilePath());
    if (restored)
      restoredSessions = *restored;
    else
      std::cerr << "Ignoring " << config.resolvedSessionFilePath() << ": "
                << restored.error().message << '\n';
  }

  RestServer server(config, application, auth);
  auto port = server.bind();
  if (!port) {
    std::cerr << port.error().message << '\n';
    return 1;
  }
  runningServer.store(&server);
  std::signal(SIGINT, requestShutdown);
  std::signal(SIGTERM, requestShutdown);

  const bool tls = config.tlsEnabled();
  std::cout << "Lexicon REST API on " << (tls ? "https://" : "http://")
            << config.listenAddress << ':' << *port << "/api/v1\n"
            << "Database: " << config.databasePath << '\n'
            << "User: " << credentials->username << '\n';
  if (config.persistSessions)
    std::cout << "Sessions: " << config.resolvedSessionFilePath() << " ("
              << restoredSessions << " still valid)\n";
  else
    std::cout << "Sessions: in memory only, a restart ends them\n";
  if (config.allowedOrigins.empty())
    std::cout << "No CORS origin is allowed yet. Browser clients need "
                 "--allowed-origin <https://your-static-host>.\n";
  else
    for (const auto &origin : config.allowedOrigins)
      std::cout << "Allowed origin: " << origin << '\n';
  if (!tls && !lexicon::http::isLoopbackAddress(config.listenAddress))
    std::cout << "WARNING: serving plaintext HTTP on a non-loopback address. "
                 "Passwords and session tokens are exposed on the network.\n";
  std::cout << "This server never serves lexicon-web; deploy it separately.\n"
            << std::flush;

  const auto listened = server.listen();
  runningServer.store(nullptr);
  if (!listened) {
    std::cerr << listened.error().message << '\n';
    return 1;
  }
  std::cout << "Stopped.\n";
  return 0;
}
} // namespace

int main(int argc, char *argv[]) {
#ifdef _WIN32
  // So a path printed back to the operator is readable rather than mojibake.
  SetConsoleOutputCP(CP_UTF8);
#else
  // Everything this process creates - a new database and its journal, blob
  // directories, staging files - holds private notes, so none of it should be
  // readable by other accounts on a shared machine. The credentials file and
  // blob files were already 0600; this covers the rest.
  ::umask(077);
#endif
  const auto arguments = lexicon::http::commandLineArguments(argc, argv);
  if (!arguments) {
    std::cerr << arguments.error().message << '\n';
    return 2;
  }
  auto parsed = lexicon::http::parseCommandLine(*arguments);
  if (!parsed) {
    std::cerr << parsed.error().message << "\n\n"
              << lexicon::http::usageText();
    return 2;
  }
  switch (parsed->command) {
  case Command::Help:
    std::cout << lexicon::http::usageText();
    return 0;
  case Command::Version:
    std::cout << kVersion << '\n';
    return 0;
  case Command::AuthSetUser:
    return authSetUser(parsed->config);
  case Command::AuthShow:
    return authShow(parsed->config);
  case Command::Serve:
    break;
  }
  return serve(parsed->config);
}
