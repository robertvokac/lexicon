#include "RestServer.h"

#include "Exchange.h"
#include "ImageValue.h"
#include "Utf8Path.h"
#include "TempFile.h"
#include "Transport.h"

// CPPHTTPLIB_OPENSSL_SUPPORT is defined by the build so that every translation
// unit that includes httplib.h agrees on the TLS-enabled layout.
#include <httplib.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <utility>

namespace lexicon::http {
namespace {
namespace fs = std::filesystem;
using httplib::Request;
using httplib::Response;

constexpr int kApiVersion = 1;
constexpr const char *kJsonContentType = "application/json; charset=utf-8";
constexpr const char *kBinaryContentType = "application/octet-stream";
// Where the static web client lives when --web-dir is given.
constexpr const char *kWebMount = "/web";
// What the client itself asks for (lexicon-web/index.html) plus
// frame-ancestors, which a page cannot set for itself. Both policies apply,
// so they must agree: a browser enforces every policy it is given.
constexpr const char *kWebContentSecurityPolicy =
    "default-src 'self'; script-src 'self'; style-src 'self'; "
    "img-src 'self' data: blob: http: https:; connect-src 'self' http: https:; "
    "object-src 'none'; base-uri 'none'; form-action 'none'; "
    "frame-ancestors 'none'";

// The mount itself and everything under it, but not "/website".
bool isWebPath(const std::string &path) {
  const std::string mount = kWebMount;
  return path == mount || path.starts_with(mount + "/");
}

std::string lowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](char ch) {
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(ch)));
  });
  return value;
}

// The origin a browser gives a page this server served: the scheme it
// listens with and the Host it was asked for. Behind a proxy that terminates
// TLS the public scheme arrives in X-Forwarded-Proto, which is believed only
// where a trusted proxy is configured. An Origin header cannot be forged by
// a page, so matching it against this server's own name identifies our own
// client, not another site.
std::string ownOrigin(const Request &request, bool tls,
                      bool trustForwardedProto) {
  const auto host = request.get_header_value("Host");
  if (host.empty())
    return {};
  bool secure = tls;
  if (trustForwardedProto) {
    const auto forwarded = lowerAscii(request.get_header_value("X-Forwarded-Proto"));
    if (forwarded == "https")
      secure = true;
    else if (forwarded == "http")
      secure = false;
  }
  return (secure ? "https://" : "http://") + host;
}

// "application/json; charset=utf-8" and "application/json" are accepted;
// anything else is rejected instead of being sniffed.
bool isContentType(const std::string &header, const char *expected) {
  const auto semicolon = header.find(';');
  const auto base = lexicon::trim(
      semicolon == std::string::npos ? header : header.substr(0, semicolon));
  return lowerAscii(base) == expected;
}

// The server's clock as UTC "YYYY-MM-DDTHH:MM:SSZ", the form alarm times take.
std::string utcNow() {
  return std::format("{:%Y-%m-%dT%H:%M:%SZ}",
                     std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
}

std::string bearerToken(const Request &request) {
  const auto header = request.get_header_value("Authorization");
  constexpr std::string_view prefix = "Bearer ";
  if (header.size() <= prefix.size() ||
      lowerAscii(header.substr(0, prefix.size())) != "bearer ")
    return {};
  return lexicon::trim(header.substr(prefix.size()));
}

void respondJson(Response &response, int status, const Json &body) {
  response.status = status;
  // A historical database may hold text that is not valid UTF-8. Replacing
  // those bytes keeps such an item readable instead of failing the whole
  // response.
  response.set_content(
      body.dump(-1, ' ', false, Json::error_handler_t::replace),
      kJsonContentType);
}

void respondNoContent(Response &response) {
  response.status = 204;
  response.body.clear();
}

void respondFailure(Response &response, const ApiFailure &failure) {
  respondJson(response, failure.status, errorBody(failure));
}

// Domain errors with Storage code carry SQL or paths, so the detail is logged
// here and never sent to the client.
void respondError(Response &response, const Error &error, const char *what) {
  const auto failure = toApiFailure(error);
  if (error.code == Error::Code::Storage)
    std::cerr << "lexicon-http: " << what << " failed: " << error.message
              << '\n';
  respondFailure(response, failure);
}

ApiFailure failureForStatus(int status) {
  switch (status) {
  case 400:
    return {400, "bad_request", "The request could not be understood."};
  case 401:
    return {401, "unauthorized", "Authentication is required."};
  case 403:
    return {403, "forbidden", "The request is not allowed."};
  case 404:
    return {404, "not_found", "The requested endpoint does not exist."};
  case 405:
    return {405, "method_not_allowed",
            "The method is not allowed for this endpoint."};
  case 411:
    return {411, "length_required", "A Content-Length header is required."};
  case 413:
    return {413, "payload_too_large", "The request body is too large."};
  case 415:
    return {415, "unsupported_media_type",
            "The request content type is not supported."};
  case 409:
    return {409, "conflict", "The record was changed by someone else."};
  case 429:
    return {429, "too_many_requests", "Too many requests."};
  default:
    break;
  }
  return {status >= 400 ? status : 500, "internal",
          "The server could not complete the request."};
}

bool validBlobHash(const std::string &hash) {
  return hash.size() == 64 && std::all_of(hash.begin(), hash.end(), [](char ch) {
           return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
         });
}

// Serializes every database and blob operation. The SQLite repository owns one
// connection and is not safe for concurrent use.
class GuardedApplication {
public:
  explicit GuardedApplication(LexiconApplication &application)
      : application_(application) {}
  template <class Operation> auto with(Operation &&operation) {
    std::lock_guard lock(mutex_);
    return operation(application_);
  }

private:
  LexiconApplication &application_;
  std::mutex mutex_;
};
} // namespace

struct RestServer::Impl {
  Impl(ServerConfig configuration, LexiconApplication &application,
       AuthState &authentication)
      : config(std::move(configuration)), guarded(application),
        auth(authentication),
        allowedOrigins(config.allowedOrigins.begin(),
                       config.allowedOrigins.end()) {}

  ServerConfig config;
  GuardedApplication guarded;
  AuthState &auth;
  std::set<std::string> allowedOrigins;
  std::set<std::string> publicRoutes;
  std::unique_ptr<httplib::Server> server;
  int boundPort = -1;

  void createServer();
  void registerRoutes();
  std::string databaseDirectory() const;

  // Request helpers -------------------------------------------------------
  std::optional<Json> jsonBody(const Request &request, Response &response) const;
  std::optional<int> pathId(const Request &request, Response &response,
                            const char *name) const;
  bool applyCors(const Request &request, Response &response) const;
};

namespace {
std::string queryValue(const Request &request, const char *key) {
  return request.has_param(key) ? request.get_param_value(key) : std::string{};
}
} // namespace

std::string RestServer::Impl::databaseDirectory() const {
  const auto database = utf8Path(config.databasePath);
  const auto directory = database.parent_path();
  return directory.empty() ? std::string(".") : pathToUtf8(directory);
}

std::optional<Json> RestServer::Impl::jsonBody(const Request &request,
                                               Response &response) const {
  if (!isContentType(request.get_header_value("Content-Type"),
                     "application/json")) {
    respondFailure(response, {415, "unsupported_media_type",
                              "Content-Type must be application/json."});
    return std::nullopt;
  }
  if (request.body.size() > config.maxJsonBytes) {
    respondFailure(response, {413, "payload_too_large",
                              "The JSON request body is too large."});
    return std::nullopt;
  }
  Json parsed = Json::parse(request.body, nullptr, false);
  if (parsed.is_discarded()) {
    respondFailure(response,
                   {400, "malformed_json", "The request body is not valid JSON."});
    return std::nullopt;
  }
  if (!parsed.is_object()) {
    respondFailure(response, {400, "malformed_json",
                              "The request body must be a JSON object."});
    return std::nullopt;
  }
  return parsed;
}

std::optional<int> RestServer::Impl::pathId(const Request &request,
                                            Response &response,
                                            const char *name) const {
  const auto found = request.path_params.find(name);
  const std::optional<int> id = found == request.path_params.end()
                                    ? std::optional<int>{}
                                    : parseId(found->second);
  if (!id)
    respondFailure(response, {400, "validation",
                              std::string("'") + name +
                                  "' must be a positive integer."});
  return id;
}

bool RestServer::Impl::applyCors(const Request &request,
                                 Response &response) const {
  const auto origin = request.get_header_value("Origin");
  if (origin.empty())
    return true;
  response.set_header("Vary", "Origin");
  // A browser sends Origin with every write request, same-origin ones too.
  // The client served at /web is then refused unless its own origin counts
  // as allowed - and being same-origin, it needs no CORS header.
  if (origin == ownOrigin(request, config.tlsEnabled(),
                          !config.trustedProxies.empty()))
    return true;
  if (allowedOrigins.find(origin) == allowedOrigins.end())
    return false;
  // Exact origin only. A wildcard would expose the authenticated API to any
  // site the browser visits.
  response.set_header("Access-Control-Allow-Origin", origin);
  response.set_header("Access-Control-Expose-Headers", "Content-Disposition");
  return true;
}

void RestServer::Impl::createServer() {
  if (config.tlsEnabled()) {
    auto secure = std::make_unique<httplib::SSLServer>(
        config.tlsCertificatePath.c_str(), config.tlsPrivateKeyPath.c_str());
    server = std::move(secure);
  } else {
    server = std::make_unique<httplib::Server>();
  }

  server->set_payload_max_length(config.maxBlobBytes);
  server->set_read_timeout(config.readTimeoutSeconds, 0);
  server->set_write_timeout(config.writeTimeoutSeconds, 0);
  server->set_keep_alive_timeout(config.keepAliveTimeoutSeconds);
  server->set_keep_alive_max_count(config.keepAliveMaxCount);
  server->set_tcp_nodelay(true);
  // The port must belong to one server. cpp-httplib's default is SO_REUSEPORT,
  // which lets a second LexiconServer bind the same port without an error;
  // the kernel then splits connections between the two processes. Sessions
  // live in one process's memory, so a token issued by one instance is
  // refused by the other and requests fail with 401 at random.
  server->set_socket_options([](socket_t sock) {
#ifdef _WIN32
    // SO_REUSEADDR on Windows lets another socket take over a bound port;
    // SO_EXCLUSIVEADDRUSE is how a Windows server keeps its port to itself.
    httplib::set_socket_opt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
#else
    // On POSIX, SO_REUSEADDR only lets a restart bind while old connections
    // sit in TIME_WAIT. It never admits a second listener.
    httplib::set_socket_opt(sock, SOL_SOCKET, SO_REUSEADDR, 1);
#endif
  });
  if (!config.trustedProxies.empty())
    server->set_trusted_proxies(config.trustedProxies);

  const bool tls = config.tlsEnabled();
  const bool servesWeb = !config.webDirectory.empty();
  if (servesWeb && !server->set_mount_point(kWebMount, config.webDirectory))
    std::cerr << "lexicon-http: cannot serve " << config.webDirectory
              << " at " << kWebMount << '\n';
  server->set_pre_routing_handler([this, tls, servesWeb](const Request &request,
                                                         Response &response) {
    response.set_header("X-Content-Type-Options", "nosniff");
    response.set_header("Referrer-Policy", "no-referrer");
    response.set_header("X-Frame-Options", "DENY");
    if (tls)
      response.set_header("Strict-Transport-Security",
                          "max-age=31536000; includeSubDomains");
    // The web client's own files: unlike the API they are public, they must
    // be allowed to run their scripts and styles, and a browser revalidates
    // them so an upgraded server never leaves an old client in a cache.
    if (servesWeb && isWebPath(request.path)) {
      response.set_header("Cache-Control", "no-cache");
      response.set_header("Content-Security-Policy", kWebContentSecurityPolicy);
      // Without the trailing slash every relative URL in the page would
      // resolve against the root.
      if (request.path == kWebMount) {
        response.set_redirect(std::string(kWebMount) + "/", 301);
        return httplib::Server::HandlerResponse::Handled;
      }
      return httplib::Server::HandlerResponse::Unhandled;
    }
    // The API returns private data only; no intermediary may store it.
    response.set_header("Cache-Control", "no-store");
    response.set_header("Content-Security-Policy",
                        "default-src 'none'; frame-ancestors 'none'");
    const bool originAllowed = applyCors(request, response);
    if (request.method == "OPTIONS") {
      if (!originAllowed) {
        respondFailure(response,
                       {403, "origin_not_allowed",
                        "This origin is not allowed to use the API."});
        return httplib::Server::HandlerResponse::Handled;
      }
      response.set_header("Access-Control-Allow-Methods",
                          "GET, POST, PUT, DELETE, OPTIONS");
      response.set_header("Access-Control-Allow-Headers",
                          "Authorization, Content-Type");
      response.set_header("Access-Control-Max-Age", "600");
      respondNoContent(response);
      return httplib::Server::HandlerResponse::Handled;
    }
    if (!originAllowed) {
      respondFailure(response, {403, "origin_not_allowed",
                                "This origin is not allowed to use the API."});
      return httplib::Server::HandlerResponse::Handled;
    }
    return httplib::Server::HandlerResponse::Unhandled;
  });

  // Runs after routing but before the body is read, so an unauthenticated or
  // oversized request never allocates a buffer for its payload.
  server->set_pre_request_handler([this](const Request &request,
                                         Response &response) {
    const bool isBlobUpload = request.matched_route == "/api/v1/blobs" &&
                              request.method == "POST";
    // An export file may carry files, so it gets the blob size limit.
    const bool isImport = request.matched_route == "/api/v1/import" &&
                          request.method == "POST";
    const auto limit = isBlobUpload || isImport ? config.maxBlobBytes : config.maxJsonBytes;
    if (request.has_header("Content-Length") &&
        request.get_header_value_u64("Content-Length") > limit) {
      response.set_header("Connection", "close");
      respondFailure(response, {413, "payload_too_large",
                                "The request body is too large."});
      return httplib::Server::HandlerResponse::Handled;
    }
    // Only the streaming blob upload can bound a body it has not measured
    // yet. A chunked JSON body would otherwise be buffered before its size
    // could be checked, which is a cheap way to make the server allocate.
    if (!isBlobUpload && request.has_header("Transfer-Encoding")) {
      response.set_header("Connection", "close");
      respondFailure(response, {411, "length_required",
                                "A Content-Length header is required."});
      return httplib::Server::HandlerResponse::Handled;
    }
    if (publicRoutes.find(request.matched_route) != publicRoutes.end())
      return httplib::Server::HandlerResponse::Unhandled;
    const auto user = auth.authenticate(bearerToken(request));
    if (!user) {
      response.set_header("WWW-Authenticate", "Bearer");
      respondFailure(response, {401, "unauthorized",
                                "Authentication is required."});
      return httplib::Server::HandlerResponse::Handled;
    }
    return httplib::Server::HandlerResponse::Unhandled;
  });

  server->set_error_handler([](const Request &, Response &response) {
    if (!response.body.empty())
      return;
    const auto failure = failureForStatus(response.status);
    response.set_content(errorBody(failure).dump(), kJsonContentType);
  });

  server->set_exception_handler(
      [](const Request &request, Response &response, std::exception_ptr caught) {
        std::string detail = "unknown error";
        try {
          std::rethrow_exception(caught);
        } catch (const BadRequest &bad) {
          respondFailure(response, {400, "validation", bad.message});
          return;
        } catch (const std::exception &error) {
          detail = error.what();
        } catch (...) {
        }
        std::cerr << "lexicon-http: unhandled exception for " << request.method
                  << ' ' << request.path << ": " << detail << '\n';
        respondFailure(response, {500, "internal",
                                  "The server could not complete the request."});
      });

  if (config.requestLogging)
    server->set_logger([](const Request &request, const Response &response) {
      // Query strings, headers and bodies are deliberately not logged: they
      // may carry Bearer tokens or passwords.
      std::cerr << "lexicon-http: " << request.method << ' ' << request.path
                << " -> " << response.status << '\n';
    });

  registerRoutes();
}

void RestServer::Impl::registerRoutes() {
  httplib::Server &api = *server;
  publicRoutes = {"/api/v1/health", "/api/v1/auth/login"};

  // The static web client ------------------------------------------------
  if (!config.webDirectory.empty()) {
    publicRoutes.insert("/");
    publicRoutes.insert("/web/config.js");
    // A deployment may carry its own config.js, and a real file wins: the
    // file handler runs before the routes. Without one, the client served
    // here talks to the origin it was loaded from.
    api.Get("/web/config.js", [](const Request &, Response &response) {
      response.set_content(
          "// Served by LexiconServer: the API is on this same origin.\n"
          "window.LEXICON_CONFIG = { apiBaseUrl: window.location.origin };\n",
          "application/javascript; charset=utf-8");
    });
    // Opening the port in a browser should land on the client, not on a 404.
    api.Get("/", [](const Request &, Response &response) {
      response.set_redirect(std::string(kWebMount) + "/", 302);
    });
  }

  // Health ----------------------------------------------------------------
  api.Get("/api/v1/health", [](const Request &, Response &response) {
    respondJson(response, 200,
                Json{{"status", "ok"},
                     {"apiVersion", kApiVersion},
                     {"application", "Lexicon"}});
  });

  // Authentication --------------------------------------------------------
  api.Post("/api/v1/auth/login", [this](const Request &request,
                                        Response &response) {
    auto body = jsonBody(request, response);
    if (!body)
      return;
    const auto username = requiredString(*body, "username");
    const auto password = requiredString(*body, "password");
    if (username.size() > 256 || password.size() > 1024) {
      respondFailure(response, {400, "validation",
                                "The user name or password is too long."});
      return;
    }
    const auto outcome = auth.login(request.remote_addr, username, password);
    switch (outcome.status) {
    case AuthState::LoginStatus::Ok:
      respondJson(response, 200,
                  Json{{"token", outcome.token},
                       {"username", auth.username()},
                       {"apiVersion", kApiVersion},
                       {"idleTimeoutSeconds",
                        static_cast<long long>(
                            config.sessions.idleTimeout.count())},
                       {"absoluteLifetimeSeconds",
                        static_cast<long long>(
                            config.sessions.absoluteLifetime.count())}});
      return;
    case AuthState::LoginStatus::RateLimited:
      response.set_header("Retry-After",
                          std::to_string(outcome.retryAfterSeconds));
      respondFailure(response, {429, "too_many_requests",
                                "Too many failed sign-in attempts. Try again "
                                "later."});
      return;
    case AuthState::LoginStatus::Unavailable:
      respondFailure(response, {500, "internal",
                                "The server could not verify the credentials."});
      return;
    case AuthState::LoginStatus::InvalidCredentials:
      break;
    }
    // Deliberately identical whether or not the user name exists.
    response.set_header("WWW-Authenticate", "Bearer");
    respondFailure(response,
                   {401, "unauthorized", "Invalid user name or password."});
  });

  api.Post("/api/v1/auth/logout", [this](const Request &request,
                                         Response &response) {
    auth.logout(bearerToken(request));
    respondNoContent(response);
  });

  api.Get("/api/v1/auth/me", [this](const Request &request,
                                    Response &response) {
    const auto user = auth.authenticate(bearerToken(request));
    respondJson(response, 200,
                Json{{"username", user.value_or(std::string{})},
                     {"apiVersion", kApiVersion}});
  });

  // Groups ----------------------------------------------------------------
  const auto sendGroups = [this](Response &response) {
    auto groups = guarded.with(
        [](LexiconApplication &application) { return application.groups.loadGroups(); });
    if (!groups) {
      respondError(response, groups.error(), "loadGroups");
      return;
    }
    respondJson(response, 200, Json{{"groups", toJsonArray(*groups)}});
  };

  api.Get("/api/v1/groups", [sendGroups](const Request &, Response &response) {
    sendGroups(response);
  });

  api.Get("/api/v1/groups/default", [this](const Request &, Response &response) {
    auto id = guarded.with([](LexiconApplication &application) {
      return application.groups.defaultGroupId();
    });
    if (!id) {
      respondError(response, id.error(), "defaultGroupId");
      return;
    }
    respondJson(response, 200, Json{{"groupId", *id}});
  });

  // upsertGroup does not return an ID, so the saved record is looked up by its
  // unique name inside the same lock.
  const auto saveGroup = [this](GroupRecord group, Response &response,
                                int status) {
    auto saved = guarded.with(
        [&group](LexiconApplication &application) -> Result<GroupRecord> {
          if (auto stored = application.groups.upsertGroup(group); !stored)
            return std::unexpected(stored.error());
          auto groups = application.groups.loadGroups();
          if (!groups)
            return std::unexpected(groups.error());
          const auto wanted = lexicon::asciiFold(lexicon::trim(group.name));
          for (const auto &candidate : *groups) {
            if (group.id > 0 ? candidate.id == group.id
                             : lexicon::asciiFold(candidate.name) == wanted)
              return candidate;
          }
          return std::unexpected(
              Error{Error::Code::NotFound, "Group not found."});
        });
    if (!saved) {
      respondError(response, saved.error(), "upsertGroup");
      return;
    }
    respondJson(response, status, Json{{"group", toJson(*saved)}});
  };

  api.Post("/api/v1/groups", [this, saveGroup](const Request &request,
                                               Response &response) {
    auto body = jsonBody(request, response);
    if (!body)
      return;
    auto group = groupFromJson(*body);
    if (group.id > 0) {
      respondFailure(response, {400, "validation",
                                "A new group must not carry an ID."});
      return;
    }
    group.id = -1;
    saveGroup(std::move(group), response, 201);
  });

  api.Put("/api/v1/groups/:id", [this, saveGroup](const Request &request,
                                                  Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto body = jsonBody(request, response);
    if (!body)
      return;
    auto group = groupFromJson(*body);
    group.id = *id; // The path owns the identity, never the body.
    saveGroup(std::move(group), response, 200);
  });

  api.Delete("/api/v1/groups/:id", [this](const Request &request,
                                          Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto deleted = guarded.with([id](LexiconApplication &application) {
      return application.groups.deleteGroup(*id);
    });
    if (!deleted) {
      respondError(response, deleted.error(), "deleteGroup");
      return;
    }
    respondNoContent(response);
  });

  // Types and fields ------------------------------------------------------
  api.Get("/api/v1/types", [this](const Request &request, Response &response) {
    int groupId = -1;
    const auto raw = queryValue(request, "groupId");
    if (!raw.empty()) {
      const auto parsed = parseId(raw);
      if (!parsed) {
        respondFailure(response, {400, "validation",
                                  "'groupId' must be a positive integer."});
        return;
      }
      groupId = *parsed;
    }
    auto types = guarded.with([groupId](LexiconApplication &application) {
      return application.types.loadItemTypes(groupId);
    });
    if (!types) {
      respondError(response, types.error(), "loadItemTypes");
      return;
    }
    respondJson(response, 200, Json{{"types", toJsonArray(*types)}});
  });

  const auto saveType = [this](ItemTypeRecord type, Response &response,
                               int status) {
    auto saved = guarded.with(
        [&type](LexiconApplication &application) -> Result<ItemTypeRecord> {
          if (auto stored = application.types.upsertItemType(type); !stored)
            return std::unexpected(stored.error());
          auto types = application.types.loadItemTypes(-1);
          if (!types)
            return std::unexpected(types.error());
          const auto wanted = lexicon::asciiFold(lexicon::trim(type.name));
          for (const auto &candidate : *types) {
            if (type.id > 0 ? candidate.id == type.id
                            : (lexicon::asciiFold(candidate.name) == wanted &&
                               candidate.groupId == type.groupId))
              return candidate;
          }
          return std::unexpected(Error{Error::Code::NotFound, "Type not found."});
        });
    if (!saved) {
      respondError(response, saved.error(), "upsertItemType");
      return;
    }
    respondJson(response, status, Json{{"type", toJson(*saved)}});
  };

  api.Post("/api/v1/types", [this, saveType](const Request &request,
                                             Response &response) {
    auto body = jsonBody(request, response);
    if (!body)
      return;
    auto type = typeFromJson(*body);
    if (type.id > 0) {
      respondFailure(response,
                     {400, "validation", "A new type must not carry an ID."});
      return;
    }
    type.id = -1;
    saveType(std::move(type), response, 201);
  });

  api.Put("/api/v1/types/:id", [this, saveType](const Request &request,
                                                Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto body = jsonBody(request, response);
    if (!body)
      return;
    auto type = typeFromJson(*body);
    type.id = *id;
    saveType(std::move(type), response, 200);
  });

  api.Delete("/api/v1/types/:id", [this](const Request &request,
                                         Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto deleted = guarded.with([id](LexiconApplication &application) {
      return application.types.deleteItemType(*id);
    });
    if (!deleted) {
      respondError(response, deleted.error(), "deleteItemType");
      return;
    }
    respondNoContent(response);
  });

  // Used by the destructive-change confirmations in both clients.
  api.Get("/api/v1/types/:id/item-count", [this](const Request &request,
                                                 Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto count = guarded.with([id](LexiconApplication &application) {
      return application.types.countItemsForType(*id);
    });
    if (!count) {
      respondError(response, count.error(), "countItemsForType");
      return;
    }
    respondJson(response, 200, Json{{"count", *count}});
  });

  api.Get("/api/v1/types/:id/fields", [this](const Request &request,
                                             Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto fields = guarded.with([id](LexiconApplication &application) {
      return application.types.loadItemFields(*id);
    });
    if (!fields) {
      respondError(response, fields.error(), "loadItemFields");
      return;
    }
    respondJson(response, 200, Json{{"fields", toJsonArray(*fields)}});
  });

  const auto saveField = [this](ItemFieldRecord field, Response &response,
                                int status) {
    auto saved = guarded.with(
        [&field](LexiconApplication &application) -> Result<ItemFieldRecord> {
          if (field.id > 0) {
            // A field cannot move between types, so an update that names the
            // wrong type is rejected before anything is written.
            auto existing = application.types.loadItemFields(field.itemTypeId);
            if (!existing)
              return std::unexpected(existing.error());
            const bool belongs =
                std::any_of(existing->begin(), existing->end(),
                            [&field](const ItemFieldRecord &candidate) {
                              return candidate.id == field.id;
                            });
            if (!belongs)
              return std::unexpected(Error{Error::Code::NotFound,
                                           "Field not found in this type."});
          }
          if (auto stored = application.types.upsertItemField(field); !stored)
            return std::unexpected(stored.error());
          auto fields = application.types.loadItemFields(field.itemTypeId);
          if (!fields)
            return std::unexpected(fields.error());
          const auto wanted = lexicon::asciiFold(lexicon::trim(field.name));
          for (const auto &candidate : *fields) {
            if (field.id > 0 ? candidate.id == field.id
                             : lexicon::asciiFold(candidate.name) == wanted)
              return candidate;
          }
          return std::unexpected(
              Error{Error::Code::NotFound, "Field not found."});
        });
    if (!saved) {
      respondError(response, saved.error(), "upsertItemField");
      return;
    }
    respondJson(response, status, Json{{"field", toJson(*saved)}});
  };

  api.Post("/api/v1/types/:id/fields", [this, saveField](const Request &request,
                                                         Response &response) {
    auto typeId = pathId(request, response, "id");
    if (!typeId)
      return;
    auto body = jsonBody(request, response);
    if (!body)
      return;
    auto field = fieldFromJson(*body);
    if (field.id > 0) {
      respondFailure(response,
                     {400, "validation", "A new field must not carry an ID."});
      return;
    }
    field.id = -1;
    field.itemTypeId = *typeId;
    saveField(std::move(field), response, 201);
  });

  api.Put("/api/v1/fields/:id", [this, saveField](const Request &request,
                                                  Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto body = jsonBody(request, response);
    if (!body)
      return;
    auto field = fieldFromJson(*body);
    field.id = *id;
    if (field.itemTypeId <= 0) {
      respondFailure(response, {400, "validation",
                                "'itemTypeId' is required when updating a "
                                "field."});
      return;
    }
    saveField(std::move(field), response, 200);
  });

  api.Delete("/api/v1/fields/:id", [this](const Request &request,
                                          Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto deleted = guarded.with([id](LexiconApplication &application) {
      return application.types.deleteItemField(*id);
    });
    if (!deleted) {
      respondError(response, deleted.error(), "deleteItemField");
      return;
    }
    respondNoContent(response);
  });

  api.Get("/api/v1/fields/:id/value-count", [this](const Request &request,
                                                   Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto count = guarded.with([id](LexiconApplication &application) {
      return application.types.countFieldValues(*id);
    });
    if (!count) {
      respondError(response, count.error(), "countFieldValues");
      return;
    }
    respondJson(response, 200, Json{{"count", *count}});
  });

  // Items -----------------------------------------------------------------
  api.Post("/api/v1/items/query", [this](const Request &request,
                                         Response &response) {
    auto body = jsonBody(request, response);
    if (!body)
      return;
    const auto query = itemQueryFromJson(*body);
    struct Page {
      std::vector<ItemRecord> items;
      int totalCount = 0;
    };
    auto page = guarded.with(
        [&query](LexiconApplication &application) -> Result<Page> {
          auto total = application.items.countItems(
              query.groupId, query.typeId, query.valueFilters, query.searchText,
              query.columnFilters, query.propertyFilters, query.tagFilter,
              query.flagFilter, query.understandingFilter, query.statusFilter,
              query.pinnedFilter);
          if (!total)
            return std::unexpected(total.error());
          auto items = application.items.loadItems(
              query.groupId, query.typeId, query.valueFilters, query.searchText,
              query.columnFilters, query.propertyFilters, query.tagFilter,
              query.flagFilter, query.understandingFilter, query.statusFilter,
              query.pinnedFilter, query.limit, query.offset, query.sortColumn,
              query.sortOrder);
          if (!items)
            return std::unexpected(items.error());
          return Page{std::move(*items), *total};
        });
    if (!page) {
      respondError(response, page.error(), "loadItems");
      return;
    }
    respondJson(response, 200,
                Json{{"items", toJsonArray(page->items)},
                     {"totalCount", page->totalCount}});
  });

  api.Get("/api/v1/items/resolve", [this](const Request &request,
                                          Response &response) {
    const auto title = queryValue(request, "title");
    if (lexicon::trim(title).empty()) {
      respondFailure(response,
                     {400, "validation", "'title' must not be empty."});
      return;
    }
    const auto disambiguation = queryValue(request, "disambiguation");
    auto id = guarded.with([&](LexiconApplication &application) {
      return application.search.findItemId(title, disambiguation);
    });
    if (!id) {
      respondError(response, id.error(), "findItemId");
      return;
    }
    respondJson(response, 200, Json{{"itemId", *id}});
  });

  api.Get("/api/v1/items/:id", [this](const Request &request,
                                      Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    bool withLinks = false;
    bool withBacklinks = false;
    const auto include = queryValue(request, "include");
    std::size_t start = 0;
    while (start <= include.size() && !include.empty()) {
      const auto end = include.find(',', start);
      const auto part = lexicon::trim(include.substr(
          start, end == std::string::npos ? std::string::npos : end - start));
      if (part == "links")
        withLinks = true;
      else if (part == "backlinks")
        withBacklinks = true;
      else if (!part.empty()) {
        respondFailure(response, {400, "validation",
                                  "'include' accepts links and backlinks."});
        return;
      }
      if (end == std::string::npos)
        break;
      start = end + 1;
    }
    struct Bundle {
      ItemRecord item;
      std::vector<LinkRecord> links;
      std::vector<LinkRecord> backlinks;
    };
    auto bundle = guarded.with(
        [&](LexiconApplication &application) -> Result<Bundle> {
          auto item = application.items.loadItem(*id);
          if (!item)
            return std::unexpected(item.error());
          Bundle result;
          result.item = std::move(*item);
          if (withLinks) {
            auto links = application.links.loadLinks(*id);
            if (!links)
              return std::unexpected(links.error());
            result.links = std::move(*links);
          }
          if (withBacklinks) {
            auto backlinks = application.links.loadBacklinks(*id);
            if (!backlinks)
              return std::unexpected(backlinks.error());
            result.backlinks = std::move(*backlinks);
          }
          return result;
        });
    if (!bundle) {
      respondError(response, bundle.error(), "loadItem");
      return;
    }
    Json body{{"item", toJson(bundle->item)}};
    if (withLinks)
      body["links"] = toJsonArray(bundle->links);
    if (withBacklinks)
      body["backlinks"] = toJsonArray(bundle->backlinks);
    respondJson(response, 200, body);
  });

  // The item, its outgoing links and its backlinks are saved in one unit of
  // work by ItemService, exactly as in the Qt dialog.
  const auto saveItem = [this](const Json &body, int itemId,
                               Response &response) {
    const auto itemJson = body.find("item");
    if (itemJson == body.end() || !itemJson->is_object()) {
      respondFailure(response,
                     {400, "validation", "'item' must be a JSON object."});
      return;
    }
    auto item = itemFromJson(*itemJson);
    item.id = itemId; // -1 creates, a positive value updates.
    if (itemId <= 0)
      item.revision = 0;
    auto links = body.contains("links") ? linksFromJson(body.at("links"))
                                        : std::vector<LinkRecord>{};
    auto backlinks = body.contains("backlinks")
                         ? linksFromJson(body.at("backlinks"))
                         : std::vector<LinkRecord>{};
    struct Saved {
      int id = -1;
      ItemRecord item;
    };
    auto saved = guarded.with(
        [&](LexiconApplication &application) -> Result<Saved> {
          auto id = itemId > 0
                        ? application.items.saveItemWithLinks(item, links,
                                                              backlinks)
                        : application.items.createItem(item, links, backlinks);
          if (!id)
            return std::unexpected(id.error());
          auto stored = application.items.loadItem(*id);
          if (!stored)
            return std::unexpected(stored.error());
          return Saved{*id, std::move(*stored)};
        });
    if (!saved) {
      respondError(response, saved.error(), "saveItemWithLinks");
      return;
    }
    respondJson(response, itemId > 0 ? 200 : 201,
                Json{{"id", saved->id}, {"item", toJson(saved->item)}});
  };

  api.Post("/api/v1/items", [this, saveItem](const Request &request,
                                             Response &response) {
    auto body = jsonBody(request, response);
    if (!body)
      return;
    const auto itemJson = body->find("item");
    if (itemJson != body->end() && itemJson->is_object() &&
        optionalId(*itemJson, "id") > 0) {
      respondFailure(response,
                     {400, "validation", "A new item must not carry an ID."});
      return;
    }
    saveItem(*body, -1, response);
  });

  api.Put("/api/v1/items/:id", [this, saveItem](const Request &request,
                                                Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto body = jsonBody(request, response);
    if (!body)
      return;
    saveItem(*body, *id, response);
  });

  api.Delete("/api/v1/items/:id", [this](const Request &request,
                                         Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto deleted = guarded.with([id](LexiconApplication &application) {
      return application.items.deleteItem(*id);
    });
    if (!deleted) {
      respondError(response, deleted.error(), "deleteItem");
      return;
    }
    respondNoContent(response);
  });

  api.Post("/api/v1/items/:id/read", [this](const Request &request,
                                            Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto logged = guarded.with([id](LexiconApplication &application) {
      return application.items.logItemRead(*id);
    });
    if (!logged) {
      respondError(response, logged.error(), "logItemRead");
      return;
    }
    respondNoContent(response);
  });

  api.Get("/api/v1/items/:id/graph", [this](const Request &request, Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    const auto number = [&](const char *key, int fallback, int highest) -> std::optional<int> {
      const auto raw = queryValue(request, key);
      if (raw.empty())
        return fallback;
      const auto parsed = parseId(raw);
      if (!parsed || *parsed > highest) {
        respondFailure(response, {400, "validation",
                                  std::string("'") + key + "' must be between 1 and " +
                                      std::to_string(highest) + "."});
        return std::nullopt;
      }
      return parsed;
    };
    const auto depth = number("depth", 2, 3);
    if (!depth)
      return;
    const auto limit = number("limit", 100, 300);
    if (!limit)
      return;
    auto graph = guarded.with([&](LexiconApplication &application) {
      return application.links.neighborhood(*id, *depth, *limit);
    });
    if (!graph) {
      respondError(response, graph.error(), "neighborhood");
      return;
    }
    Json nodes = Json::array();
    for (const auto &node : graph->nodes) {
      Json entry = toJson(node.item);
      entry["depth"] = node.depth;
      nodes.push_back(std::move(entry));
    }
    respondJson(response, 200, Json{{"nodes", std::move(nodes)},
                                    {"edges", toJsonArray(graph->edges)},
                                    {"truncated", graph->truncated}});
  });

  api.Post("/api/v1/items/:id/review", [this](const Request &request, Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto body = jsonBody(request, response);
    if (!body)
      return;
    const auto name = requiredString(*body, "rating");
    const auto rating = reviewRatingFromName(name);
    if (!rating) {
      respondFailure(response, {400, "validation",
                                "'rating' is one of Again, Hard, Good and Easy."});
      return;
    }
    auto item = guarded.with([&](LexiconApplication &application) {
      return application.review.review(*id, *rating);
    });
    if (!item) {
      respondError(response, item.error(), "review");
      return;
    }
    respondJson(response, 200, Json{{"item", toJson(*item)}});
  });

  api.Get("/api/v1/review", [this](const Request &request, Response &response) {
    int groupId = -1;
    if (const auto raw = queryValue(request, "groupId"); !raw.empty()) {
      const auto parsed = parseId(raw);
      if (!parsed) {
        respondFailure(response, {400, "validation", "'groupId' must be a positive integer."});
        return;
      }
      groupId = *parsed;
    }
    int limit = 20;
    if (const auto raw = queryValue(request, "limit"); !raw.empty()) {
      const auto parsed = parseId(raw);
      if (!parsed || *parsed > 200) {
        respondFailure(response, {400, "validation", "'limit' must be between 1 and 200."});
        return;
      }
      limit = *parsed;
    }
    struct Queue {
      std::vector<ItemRecord> items;
      int dueCount = 0;
    };
    auto queue = guarded.with([&](LexiconApplication &application) -> Result<Queue> {
      auto items = application.review.queue(groupId, limit);
      if (!items)
        return std::unexpected(items.error());
      auto due = application.review.countDue(groupId);
      if (!due)
        return std::unexpected(due.error());
      return Queue{std::move(*items), *due};
    });
    if (!queue) {
      respondError(response, queue.error(), "reviewQueue");
      return;
    }
    respondJson(response, 200, Json{{"items", toJsonArray(queue->items)}, {"dueCount", queue->dueCount}});
  });

  // Cards -----------------------------------------------------------------
  // Questions and answers about an item, and a quiz over them. Not Review: an
  // answer counts on the card and never touches the item's review schedule.
  api.Get("/api/v1/items/:id/cards", [this](const Request &request, Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto cards = guarded.with([id](LexiconApplication &application) { return application.cards.loadCards(*id); });
    if (!cards) {
      respondError(response, cards.error(), "loadCards");
      return;
    }
    respondJson(response, 200, Json{{"cards", toJsonArray(*cards)}});
  });

  api.Post("/api/v1/items/:id/cards", [this](const Request &request, Response &response) {
    auto itemId = pathId(request, response, "id");
    if (!itemId)
      return;
    auto body = jsonBody(request, response);
    if (!body)
      return;
    const auto card = cardFromJson(*body);
    if (card.id > 0) {
      respondFailure(response, {400, "validation", "A new card must not carry an ID."});
      return;
    }
    // A new card starts unanswered, whatever statistics the body carries.
    auto created = guarded.with([&](LexiconApplication &application) {
      return application.cards.createCard(*itemId, card.question, card.answer);
    });
    if (!created) {
      respondError(response, created.error(), "createCard");
      return;
    }
    respondJson(response, 201, Json{{"card", toJson(*created)}});
  });

  // The cards of the item alone at depth 0, or of its relationship
  // neighbourhood at 1 to 3, as the graph finds it.
  api.Get("/api/v1/items/:id/quiz-cards", [this](const Request &request, Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    int depth = 0;
    if (const auto raw = queryValue(request, "depth"); !raw.empty()) {
      if (raw.size() != 1 || raw[0] < '0' || raw[0] - '0' > CardService::kMaxDepth) {
        respondFailure(response, {400, "validation", "'depth' must be between 0 and 3."});
        return;
      }
      depth = raw[0] - '0';
    }
    int limit = 150;
    if (const auto raw = queryValue(request, "limit"); !raw.empty()) {
      const auto parsed = parseId(raw);
      if (!parsed || *parsed > 300) {
        respondFailure(response, {400, "validation", "'limit' must be between 1 and 300."});
        return;
      }
      limit = *parsed;
    }
    auto quiz = guarded.with([&](LexiconApplication &application) {
      return application.cards.quizCards(*id, depth, limit);
    });
    if (!quiz) {
      respondError(response, quiz.error(), "quizCards");
      return;
    }
    respondJson(response, 200, toJson(*quiz));
  });

  api.Get("/api/v1/cards/:id", [this](const Request &request, Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto card = guarded.with([id](LexiconApplication &application) { return application.cards.loadCard(*id); });
    if (!card) {
      respondError(response, card.error(), "loadCard");
      return;
    }
    respondJson(response, 200, Json{{"card", toJson(*card)}});
  });

  // Changes the question and the answer only. The item and the statistics in
  // the body, if any, are ignored: only a quiz answer moves the counts.
  api.Put("/api/v1/cards/:id", [this](const Request &request, Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto body = jsonBody(request, response);
    if (!body)
      return;
    const auto card = cardFromJson(*body);
    auto updated = guarded.with([&](LexiconApplication &application) {
      return application.cards.updateCard(*id, card.question, card.answer);
    });
    if (!updated) {
      respondError(response, updated.error(), "updateCard");
      return;
    }
    respondJson(response, 200, Json{{"card", toJson(*updated)}});
  });

  api.Delete("/api/v1/cards/:id", [this](const Request &request, Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto deleted = guarded.with([id](LexiconApplication &application) { return application.cards.deleteCard(*id); });
    if (!deleted) {
      respondError(response, deleted.error(), "deleteCard");
      return;
    }
    respondNoContent(response);
  });

  // A quiz answer: { "success": true } for Yes, false for No. The time is
  // the server's, never the client's.
  api.Post("/api/v1/cards/:id/attempt", [this](const Request &request, Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto body = jsonBody(request, response);
    if (!body)
      return;
    const auto success = body->find("success");
    if (success == body->end() || !success->is_boolean()) {
      respondFailure(response, {400, "validation", "'success' must be true or false."});
      return;
    }
    const bool knew = success->get<bool>();
    auto card = guarded.with([&](LexiconApplication &application) {
      return application.cards.recordAttempt(*id, knew);
    });
    if (!card) {
      respondError(response, card.error(), "recordCardAttempt");
      return;
    }
    respondJson(response, 200, Json{{"card", toJson(*card)}});
  });

  const auto sendLinks = [this](int itemId, bool incoming, Response &response) {
    auto links = guarded.with([itemId, incoming](LexiconApplication &application) {
      return incoming ? application.links.loadBacklinks(itemId)
                      : application.links.loadLinks(itemId);
    });
    if (!links) {
      respondError(response, links.error(), "loadLinks");
      return;
    }
    respondJson(response, 200,
                Json{{incoming ? "backlinks" : "links", toJsonArray(*links)}});
  };

  api.Get("/api/v1/items/:id/links", [this, sendLinks](const Request &request,
                                                       Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    sendLinks(*id, false, response);
  });

  api.Get("/api/v1/items/:id/backlinks",
          [this, sendLinks](const Request &request, Response &response) {
            auto id = pathId(request, response, "id");
            if (!id)
              return;
            sendLinks(*id, true, response);
          });

  const auto saveLink = [this](LinkRecord link, Response &response,
                               int status) {
    auto links = guarded.with(
        [&link](LexiconApplication &application)
            -> Result<std::vector<LinkRecord>> {
          if (auto saved = application.links.saveLink(link); !saved)
            return std::unexpected(saved.error());
          return application.links.loadLinks(link.fromItemId);
        });
    if (!links) {
      respondError(response, links.error(), "saveLink");
      return;
    }
    respondJson(response, status, Json{{"links", toJsonArray(*links)}});
  };

  api.Post("/api/v1/links", [this, saveLink](const Request &request,
                                             Response &response) {
    auto body = jsonBody(request, response);
    if (!body)
      return;
    auto link = linkFromJson(*body);
    if (link.id > 0) {
      respondFailure(response,
                     {400, "validation", "A new link must not carry an ID."});
      return;
    }
    link.id = -1;
    saveLink(std::move(link), response, 201);
  });

  api.Put("/api/v1/links/:id", [this, saveLink](const Request &request,
                                                Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto body = jsonBody(request, response);
    if (!body)
      return;
    auto link = linkFromJson(*body);
    link.id = *id;
    saveLink(std::move(link), response, 200);
  });

  api.Delete("/api/v1/links/:id", [this](const Request &request,
                                         Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto deleted = guarded.with([id](LexiconApplication &application) {
      return application.links.deleteLink(*id);
    });
    if (!deleted) {
      respondError(response, deleted.error(), "deleteLink");
      return;
    }
    respondNoContent(response);
  });

  // Search and usage ------------------------------------------------------
  const auto sendStrings = [this](Response &response,
                                  Result<std::vector<std::string>> values,
                                  const char *what) {
    if (!values) {
      respondError(response, values.error(), what);
      return;
    }
    respondJson(response, 200, Json{{"values", *values}});
  };

  api.Get("/api/v1/search/suggestions", [this, sendStrings](const Request &,
                                                            Response &response) {
    sendStrings(response,
                guarded.with([](LexiconApplication &application) {
                  return application.search.loadSuggestions();
                }),
                "loadSuggestions");
  });

  api.Get("/api/v1/search/item-titles", [this, sendStrings](const Request &,
                                                            Response &response) {
    sendStrings(response,
                guarded.with([](LexiconApplication &application) {
                  return application.search.loadItemTitles();
                }),
                "loadItemTitles");
  });

  const auto sendUsage = [this](Response &response,
                                Result<std::vector<UsageValueRecord>> values,
                                const char *what) {
    if (!values) {
      respondError(response, values.error(), what);
      return;
    }
    respondJson(response, 200, Json{{"values", toJsonArray(*values)}});
  };

  api.Get("/api/v1/usage/tags", [this, sendUsage](const Request &,
                                                  Response &response) {
    sendUsage(response,
              guarded.with([](LexiconApplication &application) {
                return application.search.loadTagUsage();
              }),
              "loadTagUsage");
  });

  api.Get("/api/v1/usage/flags", [this, sendUsage](const Request &,
                                                   Response &response) {
    sendUsage(response,
              guarded.with([](LexiconApplication &application) {
                return application.search.loadFlagUsage();
              }),
              "loadFlagUsage");
  });

  api.Get("/api/v1/usage/aliases", [this, sendUsage](const Request &,
                                                     Response &response) {
    sendUsage(response,
              guarded.with([](LexiconApplication &application) {
                return application.search.loadAliasUsage();
              }),
              "loadAliasUsage");
  });

  // Alarms -----------------------------------------------------------------
  api.Get("/api/v1/alarms", [this](const Request &, Response &response) {
    auto alarms = guarded.with([](LexiconApplication &application) { return application.alarms.loadAlarms(); });
    if (!alarms) {
      respondError(response, alarms.error(), "loadAlarms");
      return;
    }
    respondJson(response, 200, Json{{"alarms", toJsonArray(*alarms)}});
  });

  // Registered before /alarms/:id, which would take "due" for an ID.
  api.Get("/api/v1/alarms/due", [this](const Request &, Response &response) {
    auto due = guarded.with([](LexiconApplication &application) { return application.alarms.loadDueAlarms(); });
    if (!due) {
      respondError(response, due.error(), "loadDueAlarms");
      return;
    }
    respondJson(response, 200, Json{{"alarms", toJsonArray(*due)}, {"now", utcNow()}});
  });

  api.Post("/api/v1/alarms/:id/dismiss", [this](const Request &request, Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto dismissed = guarded.with([id](LexiconApplication &application) { return application.alarms.dismissAlarm(*id); });
    if (!dismissed) {
      respondError(response, dismissed.error(), "dismissAlarm");
      return;
    }
    respondJson(response, 200, Json{{"alarm", toJson(*dismissed)}});
  });

  api.Post("/api/v1/alarms/:id/snooze", [this](const Request &request, Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto body = jsonBody(request, response);
    if (!body)
      return;
    if (!body->is_object() || !body->contains("minutes") || !body->at("minutes").is_number_integer()) {
      respondFailure(response, {400, "validation", "'minutes' must be a whole number."});
      return;
    }
    const int minutes = body->at("minutes").get<int>();
    auto snoozed = guarded.with([&](LexiconApplication &application) { return application.alarms.snoozeAlarm(*id, minutes); });
    if (!snoozed) {
      respondError(response, snoozed.error(), "snoozeAlarm");
      return;
    }
    respondJson(response, 200, Json{{"alarm", toJson(*snoozed)}});
  });

  api.Get("/api/v1/alarms/:id", [this](const Request &request, Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto alarm = guarded.with([id](LexiconApplication &application) { return application.alarms.loadAlarm(*id); });
    if (!alarm) {
      respondError(response, alarm.error(), "loadAlarm");
      return;
    }
    respondJson(response, 200, Json{{"alarm", toJson(*alarm)}});
  });

  const auto saveAlarm = [this](AlarmRecord alarm, Response &response, int status) {
    auto saved = guarded.with([&alarm](LexiconApplication &application) { return application.alarms.saveAlarm(alarm); });
    if (!saved) {
      respondError(response, saved.error(), "saveAlarm");
      return;
    }
    respondJson(response, status, Json{{"alarm", toJson(*saved)}});
  };

  api.Post("/api/v1/alarms", [this, saveAlarm](const Request &request, Response &response) {
    auto body = jsonBody(request, response);
    if (!body)
      return;
    auto alarm = alarmFromJson(*body);
    if (alarm.id > 0) {
      respondFailure(response, {400, "validation", "A new alarm must not carry an ID."});
      return;
    }
    alarm.id = -1;
    saveAlarm(std::move(alarm), response, 201);
  });

  api.Put("/api/v1/alarms/:id", [this, saveAlarm](const Request &request, Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto body = jsonBody(request, response);
    if (!body)
      return;
    auto alarm = alarmFromJson(*body);
    alarm.id = *id;
    saveAlarm(std::move(alarm), response, 200);
  });

  api.Delete("/api/v1/alarms/:id", [this](const Request &request, Response &response) {
    auto id = pathId(request, response, "id");
    if (!id)
      return;
    auto deleted = guarded.with([id](LexiconApplication &application) { return application.alarms.deleteAlarm(*id); });
    if (!deleted) {
      respondError(response, deleted.error(), "deleteAlarm");
      return;
    }
    respondNoContent(response);
  });

  // Export and import --------------------------------------------------------
  api.Get("/api/v1/export", [this](const Request &request, Response &response) {
    const auto blobs = queryValue(request, "blobs");
    if (!blobs.empty() && blobs != "true" && blobs != "false") {
      respondFailure(response, {400, "validation", "'blobs' accepts true or false."});
      return;
    }
    auto document = guarded.with([&](LexiconApplication &application) {
      return exchange::exportDocument(application, blobs == "true");
    });
    if (!document) {
      respondError(response, document.error(), "exportDocument");
      return;
    }
    const auto now = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
    response.set_header("Content-Disposition",
                        std::format("attachment; filename=\"lexicon-{:%Y-%m-%d}.json\"", now));
    response.status = 200;
    response.set_content(std::move(*document), kJsonContentType);
  });

  api.Post("/api/v1/import", [this](const Request &request, Response &response) {
    if (!isContentType(request.get_header_value("Content-Type"), "application/json")) {
      respondFailure(response, {415, "unsupported_media_type",
                                "Content-Type must be application/json."});
      return;
    }
    auto report = guarded.with([&](LexiconApplication &application) {
      return exchange::importDocument(application, request.body);
    });
    if (!report) {
      respondError(response, report.error(), "importDocument");
      return;
    }
    respondJson(response, 200, Json{{"report", exchange::toJson(*report)}});
  });

  // Blobs -----------------------------------------------------------------
  // The browser sends bytes; the server owns the file system. No request ever
  // names a server-side path.
  api.Post(
      "/api/v1/blobs",
      [this](const Request &request, Response &response,
             const httplib::ContentReader &readBody) {
        if (!isContentType(request.get_header_value("Content-Type"),
                           kBinaryContentType)) {
          respondFailure(response,
                         {415, "unsupported_media_type",
                          "Content-Type must be application/octet-stream."});
          response.set_header("Connection", "close");
          return;
        }
        auto staging = TempFile::create(databaseDirectory());
        if (!staging) {
          respondError(response, staging.error(), "createTemporaryFile");
          return;
        }
        std::size_t total = 0;
        bool tooLarge = false;
        std::optional<Error> writeError;
        // Enough of the start to tell an image by its signature.
        std::string head;
        readBody([&](const char *data, std::size_t length) {
          if (head.size() < 16)
            head.append(data, std::min<std::size_t>(length, 16 - head.size()));
          total += length;
          if (total > config.maxBlobBytes) {
            tooLarge = true;
            return false;
          }
          if (auto written = staging->write(data, length); !written) {
            writeError = written.error();
            return false;
          }
          return true;
        });
        if (tooLarge) {
          response.set_header("Connection", "close");
          respondFailure(response, {413, "payload_too_large",
                                    "The upload exceeds the configured blob "
                                    "size limit."});
          return;
        }
        if (writeError) {
          respondError(response, *writeError, "writeTemporaryFile");
          return;
        }
        if (total == 0) {
          respondFailure(response,
                         {400, "validation", "The upload is empty."});
          return;
        }
        if (auto closed = staging->close(); !closed) {
          respondError(response, closed.error(), "closeTemporaryFile");
          return;
        }
        auto hash = guarded.with([&](LexiconApplication &application) {
          return application.blobs.importFile(staging->path());
        });
        staging->discard();
        if (!hash) {
          respondError(response, hash.error(), "importBlob");
          return;
        }
        const auto mediaType = lexicon::sniffImageType(head);
        respondJson(response, 201,
                    Json{{"hash", *hash}, {"mediaType", mediaType.empty() ? Json(nullptr) : Json(mediaType)}});
      });

  api.Get("/api/v1/blobs/:hash", [this](const Request &request,
                                        Response &response) {
    const auto found = request.path_params.find("hash");
    const std::string hash =
        found == request.path_params.end() ? std::string{} : found->second;
    if (!validBlobHash(hash)) {
      respondFailure(response, {400, "validation",
                                "A blob is addressed by its lowercase SHA-256 "
                                "hash."});
      return;
    }
    auto staging = TempFile::create(databaseDirectory());
    if (!staging) {
      respondError(response, staging.error(), "createTemporaryFile");
      return;
    }
    // exportBlob replaces the reserved path atomically after verifying the
    // stored content against its hash.
    if (auto closed = staging->close(); !closed) {
      respondError(response, closed.error(), "closeTemporaryFile");
      return;
    }
    auto exported = guarded.with([&](LexiconApplication &application) {
      return application.blobs.exportFile(hash, staging->path());
    });
    if (!exported) {
      respondError(response, exported.error(), "exportBlob");
      return;
    }
    auto stream = std::make_shared<std::ifstream>(utf8Path(staging->path()),
                                                  std::ios::binary);
    if (!*stream) {
      respondError(response,
                   Error{Error::Code::Storage, "Cannot read the exported blob."},
                   "readExportedBlob");
      return;
    }
    std::error_code sizeError;
    const auto size = fs::file_size(utf8Path(staging->path()), sizeError);
    if (sizeError) {
      respondError(response,
                   Error{Error::Code::Storage, "Cannot size the exported blob."},
                   "sizeExportedBlob");
      return;
    }
    // The staged file is removed once the response has been written.
    auto cleanup = std::make_shared<TempFile>(std::move(*staging));
    response.set_header("Content-Disposition",
                        "attachment; filename=\"" + hash + "\"");
    response.status = 200;
    response.set_content_provider(
        static_cast<std::size_t>(size), kBinaryContentType,
        [stream](std::size_t offset, std::size_t length, httplib::DataSink &sink) {
          static constexpr std::size_t kChunk = 256 * 1024;
          std::string buffer(std::min(length, kChunk), '\0');
          stream->seekg(static_cast<std::streamoff>(offset), std::ios::beg);
          stream->read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
          const auto read = static_cast<std::size_t>(stream->gcount());
          if (read == 0)
            return false;
          return sink.write(buffer.data(), read);
        },
        [stream, cleanup](bool) {
          stream->close();
          cleanup->discard();
        });
  });
}

RestServer::RestServer(ServerConfig config, LexiconApplication &application,
                       AuthState &auth)
    : impl_(std::make_unique<Impl>(std::move(config), application, auth)) {
  impl_->createServer();
}

RestServer::~RestServer() {
  if (impl_->server)
    impl_->server->stop();
}

Result<int> RestServer::bind() {
  if (!impl_->server->is_valid())
    return std::unexpected(Error{
        Error::Code::Storage,
        "The listener could not be created. With TLS, check --tls-cert and "
        "--tls-key."});
  const auto &address = impl_->config.listenAddress;
  const auto cannotBind = [&]() {
    return std::unexpected(
        Error{Error::Code::Storage,
              "Cannot bind " + address + ":" +
                  std::to_string(impl_->config.port) +
                  ". Is another LexiconServer, or another program, already "
                  "listening on that port?"});
  };
  if (impl_->config.port == 0) {
    // Port 0 asks the operating system for a free port, which tests use.
    const int port = impl_->server->bind_to_any_port(address);
    if (port < 0)
      return cannotBind();
    impl_->boundPort = port;
    return port;
  }
  if (!impl_->server->bind_to_port(address, impl_->config.port))
    return cannotBind();
  impl_->boundPort = impl_->config.port;
  return impl_->boundPort;
}

Result<void> RestServer::listen() {
  if (impl_->boundPort < 0) {
    auto bound = bind();
    if (!bound)
      return std::unexpected(bound.error());
  }
  if (!impl_->server->listen_after_bind())
    return std::unexpected(
        Error{Error::Code::Storage, "The HTTP listener stopped unexpectedly."});
  return {};
}

void RestServer::stop() { impl_->server->stop(); }

void RestServer::waitUntilReady() const { impl_->server->wait_until_ready(); }

int RestServer::boundPort() const { return impl_->boundPort; }
} // namespace lexicon::http
