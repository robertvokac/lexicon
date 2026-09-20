#pragma once
// The HTTP adapter. It owns no domain logic: every request is translated into
// calls on the same LexiconApplication services the Qt desktop client uses.
#include "AuthState.h"
#include "LexiconApplication.h"
#include "ServerConfig.h"

#include <memory>

namespace lexicon::http {
class RestServer {
public:
  RestServer(ServerConfig config, LexiconApplication &application,
             AuthState &auth);
  ~RestServer();
  RestServer(const RestServer &) = delete;
  RestServer &operator=(const RestServer &) = delete;

  // Binds the listening socket. Port 0 picks a free port, which the return
  // value reports. Call before listen() so callers know the port.
  Result<int> bind();
  // Serves until stop() is called. Returns an error if the socket fails.
  Result<void> listen();
  void stop();
  // Blocks until the server is accepting connections.
  void waitUntilReady() const;
  int boundPort() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace lexicon::http
