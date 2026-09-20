#include "HttpTestClient.h"

#include <httplib.h>

#include <utility>

namespace lexicontest {
namespace {
HttpResponse convert(const httplib::Result &result) {
  HttpResponse response;
  if (!result)
    return response;
  response.transported = true;
  response.status = result->status;
  response.body = result->body;
  for (const auto &[name, value] : result->headers)
    response.headers.emplace(name, value);
  return response;
}
} // namespace

std::string HttpResponse::header(const std::string &name) const {
  for (const auto &[key, value] : headers) {
    if (key.size() != name.size())
      continue;
    bool same = true;
    for (std::size_t i = 0; i < key.size() && same; ++i)
      same = std::tolower(static_cast<unsigned char>(key[i])) ==
             std::tolower(static_cast<unsigned char>(name[i]));
    if (same)
      return value;
  }
  return {};
}

bool HttpResponse::hasHeader(const std::string &name) const {
  return !header(name).empty();
}

struct HttpTestClient::Impl {
  std::unique_ptr<httplib::Client> client;
  std::string token;

  httplib::Headers headers(
      const std::map<std::string, std::string> &extraHeaders) const {
    httplib::Headers all;
    if (!token.empty())
      all.emplace("Authorization", "Bearer " + token);
    for (const auto &[name, value] : extraHeaders)
      all.emplace(name, value);
    return all;
  }
};

HttpTestClient::HttpTestClient(std::string host, int port, bool tls)
    : impl_(std::make_unique<Impl>()) {
  const std::string scheme = tls ? "https://" : "http://";
  impl_->client = std::make_unique<httplib::Client>(
      scheme + std::move(host) + ":" + std::to_string(port));
  impl_->client->enable_server_certificate_verification(false);
  impl_->client->set_read_timeout(20, 0);
  impl_->client->set_write_timeout(20, 0);
  impl_->client->set_connection_timeout(20, 0);
}

HttpTestClient::~HttpTestClient() = default;

void HttpTestClient::setBearerToken(std::string token) {
  impl_->token = std::move(token);
}

void HttpTestClient::clearBearerToken() { impl_->token.clear(); }

HttpResponse
HttpTestClient::get(const std::string &path,
                    const std::map<std::string, std::string> &extraHeaders) {
  return convert(impl_->client->Get(path, impl_->headers(extraHeaders)));
}

HttpResponse
HttpTestClient::post(const std::string &path, const std::string &body,
                     const std::string &contentType,
                     const std::map<std::string, std::string> &extraHeaders) {
  return convert(impl_->client->Post(path, impl_->headers(extraHeaders), body,
                                     contentType));
}

HttpResponse HttpTestClient::put(const std::string &path,
                                 const std::string &body,
                                 const std::string &contentType) {
  return convert(impl_->client->Put(path, impl_->headers({}), body, contentType));
}

HttpResponse HttpTestClient::postChunked(const std::string &path,
                                         const std::string &body,
                                         const std::string &contentType) {
  bool sent = false;
  return convert(impl_->client->Post(
      path, impl_->headers({}),
      [body, sent](std::size_t, httplib::DataSink &sink) mutable {
        if (!sent) {
          sink.write(body.data(), body.size());
          sent = true;
        } else {
          sink.done();
        }
        return true;
      },
      contentType));
}

HttpResponse HttpTestClient::remove(const std::string &path) {
  return convert(impl_->client->Delete(path, impl_->headers({})));
}

HttpResponse
HttpTestClient::options(const std::string &path,
                        const std::map<std::string, std::string> &extraHeaders) {
  return convert(impl_->client->Options(path, impl_->headers(extraHeaders)));
}
} // namespace lexicontest
