#pragma once
// A very small HTTP client used by the server tests so that requests travel
// over a real socket instead of calling handlers directly.
#include <map>
#include <memory>
#include <string>

namespace lexicontest {
struct HttpResponse {
  bool transported = false; // false when the request never reached the server
  int status = 0;
  std::string body;
  std::multimap<std::string, std::string> headers;

  std::string header(const std::string &name) const;
  bool hasHeader(const std::string &name) const;
};

class HttpTestClient {
public:
  HttpTestClient(std::string host, int port, bool tls = false);
  ~HttpTestClient();
  HttpTestClient(const HttpTestClient &) = delete;
  HttpTestClient &operator=(const HttpTestClient &) = delete;

  void setBearerToken(std::string token);
  void clearBearerToken();

  HttpResponse get(const std::string &path,
                   const std::map<std::string, std::string> &extraHeaders = {});
  HttpResponse post(const std::string &path, const std::string &body,
                    const std::string &contentType = "application/json",
                    const std::map<std::string, std::string> &extraHeaders = {});
  HttpResponse put(const std::string &path, const std::string &body,
                   const std::string &contentType = "application/json");
  // Sends the body with chunked transfer encoding and no Content-Length.
  HttpResponse postChunked(const std::string &path, const std::string &body,
                           const std::string &contentType = "application/json");
  HttpResponse remove(const std::string &path);
  HttpResponse options(const std::string &path,
                       const std::map<std::string, std::string> &extraHeaders);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace lexicontest
