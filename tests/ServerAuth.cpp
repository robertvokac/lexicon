// Authentication, session, CORS and request-hygiene behaviour of the REST
// server, exercised over real HTTP requests.
#include "support/HttpTestClient.h"
#include "support/ServerHarness.h"

#include "FilePath.h"
#include "Security.h"
#include "Utf8Path.h"
#include "RestServer.h"
#include "ServerConfig.h"
#include "TempFile.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using lexicontest::Checks;
using lexicontest::HttpTestClient;
using lexicontest::ServerHarness;
using Json = nlohmann::json;

namespace {
std::string credentials(const std::string &username,
                        const std::string &password) {
  return Json{{"username", username}, {"password", password}}.dump();
}

void checkHealth(Checks &checks) {
  ServerHarness harness;
  if (!harness.started()) {
    checks.expect(false, "harness start: " + harness.startupError());
    return;
  }
  HttpTestClient client("127.0.0.1", harness.port());
  const auto response = client.get("/api/v1/health");
  checks.expectEqual(response.status, 200, "health is public");
  const auto body = Json::parse(response.body, nullptr, false);
  checks.expect(!body.is_discarded() && body.value("status", "") == "ok",
                "health reports ok");
  checks.expectEqual(body.value("apiVersion", 0), 1, "health reports API v1");
  checks.expectEqual(response.header("Cache-Control"), "no-store",
                     "health is not cached");
  checks.expectEqual(response.header("X-Content-Type-Options"), "nosniff",
                     "health forbids content sniffing");
  checks.expect(!response.hasHeader("Strict-Transport-Security"),
                "plaintext listener does not claim HSTS");
  // Nothing about the deployment leaks through the health endpoint.
  checks.expect(response.body.find("lexicon.db") == std::string::npos &&
                    response.body.find("127.0.0.1") == std::string::npos,
                "health hides configuration");
}

void checkProtectedEndpoints(Checks &checks) {
  ServerHarness harness;
  HttpTestClient client("127.0.0.1", harness.port());
  checks.expectEqual(client.get("/api/v1/groups").status, 401,
                     "no token is rejected");
  client.setBearerToken("not-a-real-token");
  checks.expectEqual(client.get("/api/v1/groups").status, 401,
                     "an unknown token is rejected");
  client.setBearerToken("");
  const auto login =
      client.post("/api/v1/auth/login",
                  credentials(harness.options().username,
                              harness.options().password));
  checks.expectEqual(login.status, 200, "valid credentials are accepted");
  const auto body = Json::parse(login.body, nullptr, false);
  const auto token = body.value("token", std::string{});
  // 256 bits of base64url material.
  checks.expect(token.size() >= 43, "the session token is long and random");
  checks.expect(token != harness.options().password,
                "the token is not the password");
  checks.expectEqual(login.header("Cache-Control"), "no-store",
                     "login responses are never stored");
  client.setBearerToken(token);
  checks.expectEqual(client.get("/api/v1/groups").status, 200,
                     "a valid token grants access");
  const auto me = client.get("/api/v1/auth/me");
  checks.expectEqual(me.status, 200, "auth/me works with a token");
  checks.expectEqual(Json::parse(me.body, nullptr, false)
                         .value("username", std::string{}),
                     harness.options().username, "auth/me reports the user");
  checks.expectEqual(client.post("/api/v1/auth/logout", "").status, 204,
                     "logout succeeds");
  checks.expectEqual(client.get("/api/v1/groups").status, 401,
                     "the token stops working after logout");
}

void checkInvalidLogin(Checks &checks) {
  ServerHarness harness;
  HttpTestClient client("127.0.0.1", harness.port());
  const auto wrongPassword = client.post(
      "/api/v1/auth/login", credentials(harness.options().username, "wrong"));
  checks.expectEqual(wrongPassword.status, 401, "a wrong password is rejected");
  const auto wrongUser =
      client.post("/api/v1/auth/login",
                  credentials("someone-else", harness.options().password));
  checks.expectEqual(wrongUser.status, 401, "an unknown user is rejected");
  // The two messages must be indistinguishable so the API cannot be used to
  // discover the configured user name.
  checks.expectEqual(wrongUser.body, wrongPassword.body,
                     "login failures are indistinguishable");
  checks.expect(wrongUser.body.find(harness.options().username) ==
                    std::string::npos,
                "login failures do not echo the user name");
}

void checkSessionExpiry(Checks &checks) {
  lexicontest::HarnessOptions options;
  options.sessions.idleTimeout = std::chrono::seconds(60);
  options.sessions.absoluteLifetime = std::chrono::seconds(120);
  ServerHarness harness(options);
  HttpTestClient client("127.0.0.1", harness.port());
  const auto login = client.post(
      "/api/v1/auth/login",
      credentials(options.username, options.password));
  client.setBearerToken(
      Json::parse(login.body, nullptr, false).value("token", std::string{}));
  checks.expectEqual(client.get("/api/v1/groups").status, 200,
                     "a fresh session works");
  harness.auth().advanceClockForTests(std::chrono::seconds(61));
  checks.expectEqual(client.get("/api/v1/groups").status, 401,
                     "an idle session expires");

  const auto second = client.post(
      "/api/v1/auth/login",
      credentials(options.username, options.password));
  client.setBearerToken(
      Json::parse(second.body, nullptr, false).value("token", std::string{}));
  // Keep the session active, but push past the absolute lifetime.
  for (int step = 0; step < 3; ++step) {
    harness.auth().advanceClockForTests(std::chrono::seconds(45));
    client.get("/api/v1/groups");
  }
  checks.expectEqual(client.get("/api/v1/groups").status, 401,
                     "the absolute session lifetime is enforced");
}

void checkRateLimiting(Checks &checks) {
  lexicontest::HarnessOptions options;
  options.loginLimits.maxFailuresPerClient = 3;
  options.loginLimits.window = std::chrono::seconds(60);
  ServerHarness harness(options);
  HttpTestClient client("127.0.0.1", harness.port());
  const auto attempt = [&] {
    return client.post("/api/v1/auth/login",
                       credentials(options.username, "definitely-wrong"));
  };
  checks.expectEqual(attempt().status, 401, "failure 1 is a plain rejection");
  checks.expectEqual(attempt().status, 401, "failure 2 is a plain rejection");
  checks.expectEqual(attempt().status, 401, "failure 3 is a plain rejection");
  const auto limited = attempt();
  checks.expectEqual(limited.status, 429, "repeated failures are rate limited");
  checks.expect(!limited.header("Retry-After").empty(),
                "a rate limited login says when to retry");
  // A correct password is still rejected while the client is limited.
  checks.expectEqual(client
                         .post("/api/v1/auth/login",
                               credentials(options.username, options.password))
                         .status,
                     429, "the limit also covers correct credentials");
  harness.auth().advanceClockForTests(std::chrono::seconds(61));
  const auto afterWindow = client.post(
      "/api/v1/auth/login", credentials(options.username, options.password));
  checks.expectEqual(afterWindow.status, 200,
                     "the limit expires with its window");
  // A success clears the failure state for that client.
  checks.expectEqual(attempt().status, 401, "failures restart after success");
}

void checkCors(Checks &checks) {
  lexicontest::HarnessOptions options;
  options.allowedOrigins = {"https://lexicon.example.com"};
  ServerHarness harness(options);
  HttpTestClient client("127.0.0.1", harness.port());

  const auto preflight =
      client.options("/api/v1/items/query",
                     {{"Origin", "https://lexicon.example.com"},
                      {"Access-Control-Request-Method", "POST"},
                      {"Access-Control-Request-Headers", "authorization"}});
  checks.expectEqual(preflight.status, 204, "a preflight request succeeds");
  checks.expectEqual(preflight.header("Access-Control-Allow-Origin"),
                     "https://lexicon.example.com",
                     "the exact origin is echoed");
  checks.expectEqual(preflight.header("Vary"), "Origin",
                     "responses vary by origin");
  checks.expect(preflight.header("Access-Control-Allow-Origin") != "*",
                "the API never allows every origin");
  checks.expect(preflight.header("Access-Control-Allow-Headers")
                        .find("Authorization") != std::string::npos,
                "the Authorization header is allowed");
  checks.expect(preflight.header("Access-Control-Allow-Methods")
                        .find("DELETE") != std::string::npos,
                "REST verbs are allowed");

  const auto allowed =
      client.get("/api/v1/health", {{"Origin", "https://lexicon.example.com"}});
  checks.expectEqual(allowed.status, 200, "an allowed origin can call the API");
  checks.expectEqual(allowed.header("Access-Control-Allow-Origin"),
                     "https://lexicon.example.com",
                     "simple responses carry the origin");

  const auto rejected =
      client.get("/api/v1/health", {{"Origin", "https://evil.example"}});
  checks.expectEqual(rejected.status, 403, "an unlisted origin is rejected");
  checks.expect(rejected.header("Access-Control-Allow-Origin").empty(),
                "an unlisted origin receives no CORS grant");
  const auto rejectedPreflight =
      client.options("/api/v1/items/query",
                     {{"Origin", "https://evil.example"},
                      {"Access-Control-Request-Method", "POST"}});
  checks.expectEqual(rejectedPreflight.status, 403,
                     "an unlisted origin cannot preflight");
}

void checkRequestHygiene(Checks &checks) {
  lexicontest::HarnessOptions options;
  options.maxJsonBytes = 4096;
  ServerHarness harness(options);
  HttpTestClient client("127.0.0.1", harness.port());
  const auto login = client.post(
      "/api/v1/auth/login", credentials(options.username, options.password));
  client.setBearerToken(
      Json::parse(login.body, nullptr, false).value("token", std::string{}));

  checks.expectEqual(client.post("/api/v1/items/query", "{ not json").status,
                     400, "malformed JSON is rejected");
  checks.expectEqual(
      client.post("/api/v1/items/query", "[]").status, 400,
      "a JSON array body is rejected");
  checks.expectEqual(
      client.post("/api/v1/items/query", "{}", "text/plain").status, 415,
      "a wrong content type is rejected");
  const std::string oversized =
      Json{{"searchText", std::string(8192, 'x')}}.dump();
  const auto tooLarge = client.post("/api/v1/items/query", oversized);
  checks.expect(!tooLarge.transported || tooLarge.status == 413,
                "an oversized JSON body is rejected");
  checks.expectEqual(
      client.post("/api/v1/items/query", R"({"statusFilter":"Nonsense"})")
          .status,
      400, "an unknown enum name is rejected");
  // A body of unknown length on a JSON route cannot be size checked before it
  // is buffered, so it is refused outright.
  const auto chunked = client.postChunked("/api/v1/items/query", "{}");
  checks.expect(!chunked.transported || chunked.status == 411,
                "a chunked JSON body is refused (status " +
                    std::to_string(chunked.status) + ")");
  const auto badEnum =
      client.post("/api/v1/items/query", R"({"sortOrder":"sideways"})");
  checks.expectEqual(badEnum.status, 400, "an unknown sort order is rejected");
  checks.expect(badEnum.body.find("\"error\"") != std::string::npos,
                "errors use the documented envelope");
  checks.expectEqual(client.get("/api/v1/items/12abc").status, 400,
                     "a malformed ID is rejected");
  checks.expectEqual(client.get("/api/v1/items/0").status, 400,
                     "a zero ID is rejected");
  checks.expectEqual(client.get("/api/v1/items/999999").status, 404,
                     "a missing item is reported as not found");
  checks.expectEqual(client.get("/api/v1/nonexistent").status, 404,
                     "an unknown endpoint is reported as not found");
  const auto unknown = client.get("/api/v1/nonexistent");
  checks.expect(Json::parse(unknown.body, nullptr, false).contains("error"),
                "unknown endpoints answer with JSON");
  // No endpoint accepts a server-side path. A traversal attempt either fails
  // to match a route or is rejected as a malformed hash; it never reads a file.
  for (const char *attempt : {"/api/v1/blobs/../../etc/passwd",
                              "/api/v1/blobs/..%2F..%2Fetc%2Fpasswd",
                              "/api/v1/blobs/%2Fetc%2Fpasswd"}) {
    const auto traversal = client.get(attempt);
    checks.expect(traversal.status == 400 || traversal.status == 404,
                  std::string("traversal is refused: ") + attempt);
    checks.expect(traversal.body.find("root:") == std::string::npos,
                  std::string("traversal reads no file: ") + attempt);
  }
}

void checkStorageErrorsAreOpaque(Checks &checks) {
  ServerHarness harness;
  HttpTestClient client("127.0.0.1", harness.port());
  const auto login =
      client.post("/api/v1/auth/login",
                  credentials(harness.options().username,
                              harness.options().password));
  client.setBearerToken(
      Json::parse(login.body, nullptr, false).value("token", std::string{}));

  // No such type: the SQLite scope trigger aborts the insert, which surfaces
  // as a Storage error carrying the failing statement.
  const auto response =
      client.post("/api/v1/items",
                  Json{{"item",
                        {{"groupId", 1},
                         {"title", "Storage failure"},
                         {"itemTypeId", 4242}}}}
                      .dump());
  checks.expect(response.status == 500 || response.status == 400,
                "a storage failure is reported as a server error");
  checks.expect(response.body.find("SELECT") == std::string::npos &&
                    response.body.find("INSERT") == std::string::npos &&
                    response.body.find("SQL") == std::string::npos &&
                    response.body.find(harness.databasePath()) ==
                        std::string::npos,
                "storage errors leak no SQL or paths");
}

void checkConfigurationGuards(Checks &checks) {
  using lexicon::http::ServerConfig;
  ServerConfig loopback;
  checks.expect(lexicon::http::validate(loopback).has_value(),
                "the loopback default is accepted");
  ServerConfig exposed;
  exposed.listenAddress = "0.0.0.0";
  checks.expect(!lexicon::http::validate(exposed).has_value(),
                "a public plaintext listener is refused");
  exposed.allowInsecureHttp = true;
  checks.expect(lexicon::http::validate(exposed).has_value(),
                "the refusal can be overridden explicitly");
  const auto testHttp = lexicon::http::parseCommandLine(
      {"--listen", "0.0.0.0", "--allow-http"});
  checks.expect(testHttp.has_value() && testHttp->config.allowInsecureHttp &&
                    lexicon::http::validate(testHttp->config).has_value(),
                "--allow-http explicitly permits a test HTTP listener");
  const auto legacyHttp = lexicon::http::parseCommandLine(
      {"--listen", "0.0.0.0", "--allow-insecure-http"});
  checks.expect(legacyHttp.has_value() && legacyHttp->config.allowInsecureHttp,
                "the old HTTP override remains accepted");
  ServerConfig tls;
  tls.listenAddress = "0.0.0.0";
  tls.tlsCertificatePath = "cert.pem";
  tls.tlsPrivateKeyPath = "key.pem";
  checks.expect(lexicon::http::validate(tls).has_value(),
                "a public TLS listener is accepted");
  ServerConfig halfTls;
  halfTls.tlsCertificatePath = "cert.pem";
  checks.expect(!lexicon::http::validate(halfTls).has_value(),
                "a certificate without a key is refused");
  ServerConfig badOrigin;
  badOrigin.allowedOrigins = {"*"};
  checks.expect(!lexicon::http::validate(badOrigin).has_value(),
                "a wildcard origin is refused");
  badOrigin.allowedOrigins = {"https://lexicon.example.com/app"};
  checks.expect(!lexicon::http::validate(badOrigin).has_value(),
                "an origin with a path is refused");
  badOrigin.allowedOrigins = {"https://lexicon.example.com"};
  checks.expect(lexicon::http::validate(badOrigin).has_value(),
                "an exact origin is accepted");

  // --web-dir serves a copy of lexicon-web, and nothing that holds the
  // dictionary: everything under it is public.
  namespace fs = std::filesystem;
  const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = fs::temp_directory_path() / ("lexicon-webdir-test-" + std::to_string(unique));
  std::error_code ignored;
  fs::create_directories(root / "client", ignored);
  ServerConfig web;
  web.databasePath = lexicon::pathToUtf8(root / "lexicon.db");
  web.webDirectory = lexicon::pathToUtf8(root / "client");
  checks.expect(!lexicon::http::validate(web).has_value(),
                "a web directory without index.html is refused");
  { std::ofstream page(root / "client" / "index.html"); page << "<!DOCTYPE html>"; }
  checks.expect(lexicon::http::validate(web).has_value(),
                "a copy of the client is accepted");
  web.webDirectory = lexicon::pathToUtf8(root);
  { std::ofstream page(root / "index.html"); page << "<!DOCTYPE html>"; }
  checks.expect(!lexicon::http::validate(web).has_value(),
                "a directory holding the database is refused");
  ServerConfig missing;
  missing.webDirectory = lexicon::pathToUtf8(root / "nowhere");
  checks.expect(!lexicon::http::validate(missing).has_value(),
                "a web directory that does not exist is refused");
  fs::remove_all(root, ignored);
  const auto served = lexicon::http::parseCommandLine({"--web-dir", "/srv/lexicon-web"});
  checks.expect(served.has_value() && served->config.webDirectory == "/srv/lexicon-web",
                "--web-dir is parsed");

  const auto parsed = lexicon::http::parseCommandLine(
      {"--port", "9000", "--allowed-origin", "https://a.example",
       "--allowed-origin", "https://b.example"});
  checks.expect(parsed.has_value() && parsed->config.port == 9000 &&
                    parsed->config.allowedOrigins.size() == 2,
                "repeated origins accumulate");
  checks.expect(!lexicon::http::parseCommandLine({"--port", "abc"}).has_value(),
                "a non-numeric port is refused");
  checks.expect(!lexicon::http::parseCommandLine({"--nonsense"}).has_value(),
                "an unknown option is refused");
  checks.expect(!lexicon::http::parseCommandLine({"--port"}).has_value(),
                "a missing option value is refused");
  const auto sessions = lexicon::http::parseCommandLine({"--database", "data/lexicon.db"});
  checks.expect(sessions.has_value() && sessions->config.persistSessions &&
                    lexicon::utf8Path(sessions->config.resolvedSessionFilePath()) ==
                        lexicon::utf8Path("data") / "lexicon-sessions.json",
                "sessions are kept next to the database by default");
  const auto sessionFile = lexicon::http::parseCommandLine({"--session-file", "/var/lib/s.json"});
  checks.expect(sessionFile.has_value() &&
                    sessionFile->config.resolvedSessionFilePath() == "/var/lib/s.json",
                "--session-file chooses the session file");
  const auto memoryOnly = lexicon::http::parseCommandLine({"--no-session-file"});
  checks.expect(memoryOnly.has_value() && !memoryOnly->config.persistSessions,
                "--no-session-file keeps sessions in memory");
  // Arguments arrive as UTF-8 on every platform, and a conversion failure is
  // reported rather than turned into an empty argument.
  char program[] = "LexiconServer";
  char option[] = "--port";
  char value[] = "9001";
  char *argv[] = {program, option, value};
  auto passed = lexicon::http::commandLineArguments(3, argv);
  checks.expect(passed.has_value(), "the command line is readable");
  if (passed)
    checks.expect(passed->size() == 2 && (*passed)[0] == "--port" &&
                      (*passed)[1] == "9001",
                  "argv is passed through without argv[0]");
  const auto authCommand = lexicon::http::parseCommandLine({"auth", "set-user"});
  checks.expect(authCommand.has_value() &&
                    authCommand->command == lexicon::http::Command::AuthSetUser,
                "auth set-user is recognized");
}

void checkPasswordHashing(Checks &checks) {
  const auto parameters = lexicon::http::ScryptParameters::forTests();
  auto first = lexicon::http::hashPassword("correct horse", parameters);
  auto second = lexicon::http::hashPassword("correct horse", parameters);
  checks.expect(first.has_value() && second.has_value(),
                "passwords can be hashed");
  checks.expect(first->salt != second->salt, "every hash has a unique salt");
  checks.expect(first->hash != second->hash,
                "the same password hashes differently");
  checks.expect(first->hash.size() == 32, "the derived key is 256 bits");
  const auto stored = std::string(first->hash.begin(), first->hash.end());
  checks.expect(stored.find("correct horse") == std::string::npos,
                "the password is not stored in the hash");
  auto right = lexicon::http::verifyPassword(*first, "correct horse");
  auto wrong = lexicon::http::verifyPassword(*first, "correct horse ");
  checks.expect(right.has_value() && *right, "the right password verifies");
  checks.expect(wrong.has_value() && !*wrong, "a wrong password fails");
  checks.expect(first->algorithm == "scrypt" && first->version == 1,
                "the hash records its algorithm and version");
  checks.expect(lexicon::http::ScryptParameters{}.n >= (1u << 16),
                "production parameters are intentionally expensive");

  auto tokenA = lexicon::http::randomToken();
  auto tokenB = lexicon::http::randomToken();
  checks.expect(tokenA.has_value() && tokenB.has_value() && *tokenA != *tokenB,
                "session tokens are unique");
  checks.expect(tokenA->size() >= 43, "session tokens carry 256 random bits");
}

void checkSessionFile(Checks &checks) {
  // A restart keeps the sessions that are still valid for the credentials.
  namespace fs = std::filesystem;
  using lexicon::http::AuthState;
  const auto directory =
      fs::temp_directory_path() /
      ("lexicon-sessions-test-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(directory);
  const auto file = lexicon::pathToUtf8(directory / "lexicon-sessions.json");
  const auto password = std::string("correct horse battery");
  auto hashed = lexicon::http::hashPassword(
      password, lexicon::http::ScryptParameters::forTests());
  checks.expect(hashed.has_value(), "the session test password hashes");
  if (!hashed) return;
  const lexicon::http::Credentials credentials{"lexicon", *hashed};

  std::string token;
  std::string loggedOut;
  {
    AuthState auth({}, {});
    auth.setCredentials(credentials);
    checks.expectEqual(static_cast<long long>(auth.useSessionFile(file).value_or(99)), 0,
                       "there is nothing to restore before the first sign-in");
    token = auth.login("client", "lexicon", password).token;
    loggedOut = auth.login("client", "lexicon", password).token;
    checks.expect(!token.empty() && !loggedOut.empty(), "two sessions start");
    checks.expect(auth.logout(loggedOut), "one of them logs out");
  }

  std::ifstream input(directory / "lexicon-sessions.json", std::ios::binary);
  const std::string stored((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
  checks.expect(!stored.empty(), "the sessions are written to the file");
  checks.expect(stored.find(token) == std::string::npos,
                "the file never holds a token");
  checks.expect(stored.find(lexicon::http::sha256Hex(token)) != std::string::npos,
                "the file holds the token's hash");
#ifndef _WIN32
  const auto permissions = fs::status(directory / "lexicon-sessions.json").permissions();
  checks.expect((permissions & (fs::perms::group_all | fs::perms::others_all)) ==
                    fs::perms::none,
                "the session file is private to the server's account");
#endif

  {
    AuthState restarted({}, {});
    restarted.setCredentials(credentials);
    checks.expectEqual(static_cast<long long>(restarted.useSessionFile(file).value_or(0)), 1,
                       "the open session is restored");
    checks.expect(restarted.authenticate(token).has_value(),
                  "its token works after the restart");
    checks.expect(!restarted.authenticate(loggedOut).has_value(),
                  "a logged out token stays logged out");
  }
  {
    AuthState later({}, {});
    later.setCredentials(credentials);
    later.advanceClockForTests(std::chrono::hours(9));
    checks.expectEqual(static_cast<long long>(later.useSessionFile(file).value_or(99)), 0,
                       "a session idle for longer than the timeout is not restored");
    checks.expect(!later.authenticate(token).has_value(), "and its token is refused");
  }
  {
    auto other = lexicon::http::hashPassword(
        "a new password entirely", lexicon::http::ScryptParameters::forTests());
    AuthState changed({}, {});
    changed.setCredentials({"lexicon", *other});
    checks.expectEqual(static_cast<long long>(changed.useSessionFile(file).value_or(99)), 0,
                       "a new password ends the stored sessions");
    checks.expect(!changed.authenticate(token).has_value(),
                  "the old token is refused after a password change");
  }
  {
    std::ofstream(directory / "lexicon-sessions.json", std::ios::trunc) << "not json";
    AuthState damaged({}, {});
    damaged.setCredentials(credentials);
    checks.expect(!damaged.useSessionFile(file).has_value(),
                  "a damaged session file is reported");
    const auto fresh = damaged.login("client", "lexicon", password).token;
    checks.expect(!fresh.empty(), "signing in still works");
    AuthState again({}, {});
    again.setCredentials(credentials);
    checks.expectEqual(static_cast<long long>(again.useSessionFile(file).value_or(0)), 1,
                       "and replaces the damaged file");
  }
  std::error_code ignored;
  fs::remove_all(directory, ignored);
}

void checkCredentialsFile(Checks &checks) {
  // Its own directory: the leftover scan below must see only what this test
  // created.
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("lexicon-auth-test-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code directoryError;
  std::filesystem::create_directories(directory, directoryError);
  if (directoryError) {
    checks.expect(false, "the credentials test directory can be created");
    return;
  }
  const auto path = (directory / "lexicon-auth.json").string();
  auto hashed = lexicon::http::hashPassword(
      "another passphrase", lexicon::http::ScryptParameters::forTests());
  checks.expect(hashed.has_value(), "the test password hashes");
  checks.expect(
      lexicon::http::writeCredentialsFile(path, {"user", *hashed}).has_value(),
      "credentials can be stored");
  auto loaded = lexicon::http::readCredentialsFile(path);
  checks.expect(loaded.has_value(), "credentials can be read back");
  if (loaded) {
    checks.expectEqual(loaded->username, "user", "the user name round-trips");
    auto verified =
        lexicon::http::verifyPassword(loaded->password, "another passphrase");
    checks.expect(verified.has_value() && *verified,
                  "the stored hash still verifies");
  }
  std::ifstream raw(path);
  const std::string contents((std::istreambuf_iterator<char>(raw)),
                             std::istreambuf_iterator<char>());
  checks.expect(contents.find("another passphrase") == std::string::npos,
                "the credentials file holds no plaintext password");
#ifndef _WIN32
  const auto permissions = std::filesystem::status(path).permissions();
  checks.expect((permissions & (std::filesystem::perms::group_all |
                                std::filesystem::perms::others_all)) ==
                    std::filesystem::perms::none,
                "the credentials file is readable by its owner only");
#endif

  // Changing the password rewrites the same path. This is the `auth set-user`
  // flow, and it has to work every time, not just the first.
  auto second = lexicon::http::hashPassword(
      "a different passphrase", lexicon::http::ScryptParameters::forTests());
  checks.expect(second.has_value(), "the replacement password hashes");
  checks.expect(
      lexicon::http::writeCredentialsFile(path, {"renamed", *second})
          .has_value(),
      "credentials can be written again over the same file");
  auto reloaded = lexicon::http::readCredentialsFile(path);
  checks.expect(reloaded.has_value(), "the rewritten file can be read");
  if (reloaded) {
    checks.expectEqual(reloaded->username, "renamed",
                       "the new user name replaced the old one");
    auto current =
        lexicon::http::verifyPassword(reloaded->password, "a different passphrase");
    auto previous =
        lexicon::http::verifyPassword(reloaded->password, "another passphrase");
    checks.expect(current.has_value() && *current,
                  "the new password verifies after the rewrite");
    checks.expect(previous.has_value() && !*previous,
                  "the old password no longer verifies");
  }
  // A third round, because an in-place rewrite must not depend on how the
  // file came to exist.
  checks.expect(
      lexicon::http::writeCredentialsFile(path, {"third", *second}).has_value(),
      "credentials can be written a third time");
  checks.expectEqual(
      lexicon::http::readCredentialsFile(path)
          .transform([](const lexicon::http::Credentials &value) {
            return value.username;
          })
          .value_or(std::string{}),
      "third", "the third write is the one that survives");
#ifndef _WIN32
  const auto afterRewrite = std::filesystem::status(path).permissions();
  checks.expect((afterRewrite & (std::filesystem::perms::group_all |
                                 std::filesystem::perms::others_all)) ==
                    std::filesystem::perms::none,
                "a rewritten credentials file is still owner only");
#endif
  // Nothing is left behind next to the target.
  int leftovers = 0;
  for (const auto &entry : std::filesystem::directory_iterator(directory)) {
    if (entry.path().filename() != "lexicon-auth.json")
      ++leftovers;
  }
  checks.expectEqual(leftovers, 0, "no temporary credentials file is left");

  // A write that cannot succeed must not damage what is already there.
  const auto unreachable =
      (directory / "no-such-directory" / "lexicon-auth.json").string();
  checks.expect(!lexicon::http::writeCredentialsFile(unreachable,
                                                     {"ghost", *second})
                     .has_value(),
                "writing into a missing directory fails");
  checks.expectEqual(
      lexicon::http::readCredentialsFile(path)
          .transform([](const lexicon::http::Credentials &value) {
            return value.username;
          })
          .value_or(std::string{}),
      "third", "a failed write leaves the existing credentials intact");

  std::error_code ignored;
  std::filesystem::remove_all(directory, ignored);

  const auto missing = lexicon::http::readCredentialsFile(path);
  checks.expect(!missing.has_value() &&
                    missing.error().code == lexicon::Error::Code::NotFound,
                "a missing credentials file is reported clearly");
}

// An expensive password derivation must never hold the lock that ordinary
// authenticated requests need.
void checkSlowLoginDoesNotBlockSessions(Checks &checks) {
  lexicon::http::LoginLimitPolicy limits;
  limits.maxFailuresPerClient = 100000;
  limits.maxFailuresTotal = 0;
  limits.maxConcurrentHashes = 2;
  lexicon::http::AuthState auth(lexicon::http::SessionPolicy{}, limits);

  // Costly enough that a serialized implementation would be obvious.
  auto parameters = lexicon::http::ScryptParameters::forTests();
  parameters.n = 1u << 16;
  parameters.maxMemory = 256ull * 1024 * 1024;
  auto hashed = lexicon::http::hashPassword("the real passphrase", parameters);
  if (!hashed) {
    checks.expect(false, "the slow test password hashes");
    return;
  }
  auth.setCredentials({"user", *hashed});

  const auto session = auth.login("client", "user", "the real passphrase");
  checks.expect(session.status == lexicon::http::AuthState::LoginStatus::Ok,
                "the slow-login test signs in");

  const auto measure = [](auto &&operation) {
    const auto start = std::chrono::steady_clock::now();
    operation();
    return std::chrono::steady_clock::now() - start;
  };
  const auto oneDerivation = measure(
      [&] { auth.login("client", "user", "wrong"); });

  std::atomic<bool> stop{false};
  std::atomic<int> attempts{0};
  std::vector<std::thread> workers;
  for (int worker = 0; worker < 4; ++worker) {
    workers.emplace_back([&, worker] {
      while (!stop.load()) {
        auth.login("storm-" + std::to_string(worker), "user", "wrong");
        ++attempts;
      }
    });
  }
  // Wait until the storm is actually running before timing anything.
  while (attempts.load() < 2 && !stop.load())
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

  constexpr int kChecks = 50;
  const auto authenticateTime = measure([&] {
    for (int index = 0; index < kChecks; ++index) {
      const auto user = auth.authenticate(session.token);
      if (!user)
        checks.expect(false, "the session stays valid during a login storm");
    }
  });
  stop.store(true);
  for (auto &worker : workers)
    worker.join();

  // Serialized behind scrypt, fifty of these would cost fifty derivations.
  checks.expect(authenticateTime < oneDerivation,
                "50 session checks cost less than one password derivation (" +
                    std::to_string(std::chrono::duration_cast<
                                       std::chrono::milliseconds>(
                                       authenticateTime)
                                       .count()) +
                    "ms vs " +
                    std::to_string(std::chrono::duration_cast<
                                       std::chrono::milliseconds>(
                                       oneDerivation)
                                       .count()) +
                    "ms)");
  checks.expect(auth.peakPasswordHashConcurrency() <= limits.maxConcurrentHashes,
                "password derivations stay within their concurrency bound (" +
                    std::to_string(auth.peakPasswordHashConcurrency()) + ")");
  checks.expect(auth.peakPasswordHashConcurrency() >= 2,
                "the bound is actually exercised by the storm");
}

void checkPasswordHashBoundOfOne(Checks &checks) {
  lexicon::http::LoginLimitPolicy limits;
  limits.maxFailuresPerClient = 100000;
  limits.maxFailuresTotal = 0;
  limits.maxConcurrentHashes = 1;
  lexicon::http::AuthState auth(lexicon::http::SessionPolicy{}, limits);
  auto hashed = lexicon::http::hashPassword(
      "passphrase", lexicon::http::ScryptParameters::forTests());
  if (!hashed) {
    checks.expect(false, "the bound test password hashes");
    return;
  }
  auth.setCredentials({"user", *hashed});
  std::vector<std::thread> workers;
  for (int worker = 0; worker < 8; ++worker) {
    workers.emplace_back([&, worker] {
      for (int round = 0; round < 4; ++round)
        auth.login("bound-" + std::to_string(worker), "user", "wrong");
    });
  }
  for (auto &worker : workers)
    worker.join();
  checks.expectEqual(auth.peakPasswordHashConcurrency(), 1,
                     "a bound of one serializes every derivation");
}

void checkTls(Checks &checks) {
  lexicontest::HarnessOptions options;
  options.tls = true;
  ServerHarness harness(options);
  if (!harness.started()) {
    checks.expect(false, "TLS harness start: " + harness.startupError());
    return;
  }
  HttpTestClient client("127.0.0.1", harness.port(), true);
  const auto health = client.get("/api/v1/health");
  checks.expectEqual(health.status, 200, "the TLS listener serves the API");
  checks.expect(health.header("Strict-Transport-Security")
                    .find("max-age=") != std::string::npos,
                "HSTS is sent when the server terminates TLS itself");
  const auto login =
      client.post("/api/v1/auth/login",
                  credentials(options.username, options.password));
  checks.expectEqual(login.status, 200, "credentials travel over TLS");
  client.setBearerToken(
      Json::parse(login.body, nullptr, false).value("token", std::string{}));
  checks.expectEqual(client.get("/api/v1/groups").status, 200,
                     "a TLS session reaches the domain endpoints");
}

// The conversion the Windows command line depends on. Kept portable so the
// cases below run on every platform rather than only where a Windows API is.
void checkTextConversions(Checks &checks) {
  using lexicon::http::utf16ToUtf8;
  struct Sample {
    std::u16string utf16;
    std::string utf8;
    const char *what;
  };
  const std::vector<Sample> samples = {
      {u"", "", "an empty string"},
      {u"--database", "--database", "ASCII"},
      {u"Jiří", "Jiří", "Czech letters"},
      {u"Uživatelé", "Uživatelé", "a Czech directory name"},
      {u"Český název", "Český název", "a Czech phrase"},
      {u"中文词典", "中文词典", "CJK"},
      {u"😀📘", "😀📘", "emoji, which are surrogate pairs"},
      {u"C:\\Users\\Jiří\\Lexicon\\lexikon.db",
       "C:\\Users\\Jiří\\Lexicon\\lexikon.db", "a full Windows path"},
  };
  std::vector<Sample> all = samples;
  {
    // Built rather than written as a literal, which the compiler warns about.
    Sample embedded{u"a", std::string("a\0b", 3), "an embedded null"};
    embedded.utf16.push_back(u'\0');
    embedded.utf16.push_back(u'b');
    all.push_back(std::move(embedded));
  }
  for (const auto &sample : all) {
    auto converted = utf16ToUtf8(sample.utf16);
    checks.expect(converted.has_value(),
                  std::string("UTF-16 converts: ") + sample.what);
    if (converted)
      checks.expectEqual(*converted, sample.utf8,
                         std::string("UTF-16 matches UTF-8: ") + sample.what);
    // And the result is the path the file system layer would build.
    checks.expectEqual(
        lexicon::pathToUtf8(lexicon::utf8Path(sample.utf8)),
        sample.utf8, std::string("round trips as a path: ") + sample.what);
  }
  // The core conversion both the storage and the server layer use.
  for (const auto &sample : all) {
    const auto path = lexicon::utf8Path(sample.utf8);
    checks.expectEqual(lexicon::pathToUtf8(path), sample.utf8,
                       std::string("core path conversion round trips: ") +
                           sample.what);
  }
  // An unpaired surrogate is reported rather than silently replaced.
  const std::u16string highOnly(1, static_cast<char16_t>(0xd83d));
  const std::u16string lowOnly(1, static_cast<char16_t>(0xde00));
  std::u16string reversed;
  reversed.push_back(static_cast<char16_t>(0xde00));
  reversed.push_back(static_cast<char16_t>(0xd83d));
  checks.expect(!utf16ToUtf8(highOnly).has_value(),
                "an unpaired high surrogate is rejected");
  checks.expect(!utf16ToUtf8(lowOnly).has_value(),
                "an unpaired low surrogate is rejected");
  checks.expect(!utf16ToUtf8(reversed).has_value(),
                "a reversed surrogate pair is rejected");
  // The boundary of the basic plane and of the whole range.
  checks.expectEqual(utf16ToUtf8(u"￿").value_or(""), "￿",
                     "the last character of the basic plane");
  checks.expectEqual(utf16ToUtf8(u"􏿿").value_or(""), "􏿿",
                     "the last character of Unicode");
}

// A database under a path the active code page cannot spell is the ordinary
// case for anyone whose user name is not ASCII.
void checkNonAsciiPaths(Checks &checks) {
  const auto root =
      std::filesystem::temp_directory_path() /
      lexicon::utf8Path(
          "lexicon-Ji\u0159\u00ed-\u017elu\u0165ou\u010dk\u00fd-" +
          std::to_string(
              std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code error;
  std::filesystem::create_directories(root, error);
  if (error) {
    checks.expect(false, "the non-ASCII test directory can be created");
    return;
  }
  const auto database = lexicon::pathToUtf8(root / "lexikon-databáze.db");

  lexicon::http::ServerConfig config;
  config.databasePath = database;
  const auto authFile = config.resolvedAuthFilePath();
  checks.expect(authFile.find("Ji\u0159\u00ed") != std::string::npos,
                "the credentials path keeps its non-ASCII directory: " + authFile);
  checks.expect(std::filesystem::exists(lexicon::utf8Path(authFile)
                                            .parent_path()),
                "that directory is the one that exists on disk");

  // Write, rewrite and read credentials through the non-ASCII path.
  auto hashed = lexicon::http::hashPassword(
      "passphrase", lexicon::http::ScryptParameters::forTests());
  checks.expect(hashed.has_value(), "the non-ASCII test password hashes");
  checks.expect(
      lexicon::http::writeCredentialsFile(authFile, {"jiří", *hashed})
          .has_value(),
      "credentials are written under a non-ASCII path");
  checks.expect(
      lexicon::http::writeCredentialsFile(authFile, {"jiří", *hashed})
          .has_value(),
      "and rewritten there");
  auto loaded = lexicon::http::readCredentialsFile(authFile);
  checks.expect(loaded.has_value(), "credentials are read back");
  if (loaded)
    checks.expectEqual(loaded->username, "jiří",
                       "a non-ASCII user name round-trips");
  checks.expect(std::filesystem::exists(lexicon::utf8Path(authFile)),
                "the credentials file is where the path said it would be");

  // The staging file the blob endpoints use must land in the same directory.
  auto staging = lexicon::http::TempFile::create(
      lexicon::pathToUtf8(root));
  checks.expect(staging.has_value(), "a temporary file is created there");
  if (staging) {
    const char payload[] = "obsah";
    checks.expect(staging->write(payload, sizeof(payload) - 1).has_value(),
                  "the temporary file accepts content");
    const auto target = lexicon::pathToUtf8(root / "výstup.bin");
    checks.expect(staging->replace(target).has_value(),
                  "it installs atomically under a non-ASCII path");
    checks.expect(std::filesystem::exists(lexicon::utf8Path(target)),
                  "the installed file exists");
  }

  // Round trip every path through the boundary helpers unchanged.
  const auto sample = lexicon::pathToUtf8(root / "kůň" / "žlutý.db");
  checks.expectEqual(
      lexicon::pathToUtf8(lexicon::utf8Path(sample)), sample,
      "paths survive the UTF-8 boundary unchanged");

  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

// Starting the server twice must fail, not quietly share the port.
void checkPortIsExclusive(Checks &checks) {
  ServerHarness first;
  if (!first.started()) {
    checks.expect(false, "the first server starts: " + first.startupError());
    return;
  }
  lexicon::http::ServerConfig config;
  config.databasePath = first.databasePath();
  config.listenAddress = "127.0.0.1";
  config.port = first.port();
  config.requestLogging = false;
  lexicon::http::RestServer second(config, first.application(), first.auth());
  const auto bound = second.bind();
  checks.expect(!bound.has_value(),
                "a second server on the same port is refused");
  if (!bound)
    checks.expect(bound.error().message.find("Cannot bind") != std::string::npos,
                  "and says why: " + bound.error().message);
  // The first server is untouched and still answers.
  HttpTestClient client("127.0.0.1", first.port());
  checks.expectEqual(client.get("/api/v1/health").status, 200,
                     "the first server keeps serving");
}

// scrypt runs HMAC, which zero-pads short keys, so "secret" and "secret\0"
// derive the same key. The server must not rely on that: a NUL is refused.
void checkNulInCredentials(Checks &checks) {
  ServerHarness harness;
  HttpTestClient client("127.0.0.1", harness.port());
  const auto &options = harness.options();
  auto withNul = [](std::string text) {
    text.push_back('\0');
    return text;
  };
  const auto passwordNul =
      Json{{"username", options.username}, {"password", withNul(options.password)}};
  const auto usernameNul =
      Json{{"username", withNul(options.username)}, {"password", options.password}};
  const auto first = client.post("/api/v1/auth/login", passwordNul.dump());
  checks.expectEqual(first.status, 401,
                     "the right password followed by NUL is refused");
  checks.expect(first.body.find("\"token\"") == std::string::npos,
                "and no session is issued for it");
  checks.expectEqual(client.post("/api/v1/auth/login", usernameNul.dump()).status,
                     401, "a user name ending in NUL is refused");
  // The plain credentials still work.
  checks.expectEqual(
      client
          .post("/api/v1/auth/login",
                credentials(options.username, options.password))
          .status,
      200, "the exact credentials still sign in");
  checks.expect(lexicon::http::containsNul(std::string("a\0b", 3)) &&
                    !lexicon::http::containsNul("ab"),
                "containsNul finds an embedded NUL");
}

void checkUnconfiguredServer(Checks &checks) {
  lexicontest::HarnessOptions options;
  options.configureCredentials = false;
  ServerHarness harness(options);
  HttpTestClient client("127.0.0.1", harness.port());
  checks.expectEqual(client.get("/api/v1/health").status, 200,
                     "health works without credentials");
  const auto login = client.post(
      "/api/v1/auth/login", credentials(options.username, options.password));
  checks.expectEqual(login.status, 401,
                     "an unconfigured server accepts nobody");
  checks.expect(login.body.find("not configured") == std::string::npos,
                "the server does not reveal that no user exists");
}
} // namespace

int main() {
  Checks checks;
  checkHealth(checks);
  checkProtectedEndpoints(checks);
  checkInvalidLogin(checks);
  checkSessionExpiry(checks);
  checkRateLimiting(checks);
  checkCors(checks);
  checkRequestHygiene(checks);
  checkStorageErrorsAreOpaque(checks);
  checkConfigurationGuards(checks);
  checkPasswordHashing(checks);
  checkCredentialsFile(checks);
  checkTextConversions(checks);
  checkNonAsciiPaths(checks);
  checkSlowLoginDoesNotBlockSessions(checks);
  checkPasswordHashBoundOfOne(checks);
  checkTls(checks);
  checkPortIsExclusive(checks);
  checkNulInCredentials(checks);
  checkUnconfiguredServer(checks);
  checkSessionFile(checks);
  return checks.summarize("server_auth");
}
