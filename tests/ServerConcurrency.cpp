// The SQLite repository owns one connection and is not thread safe. The HTTP
// adapter serializes every domain call, which this test exercises directly
// against a running server.
#include "support/HttpTestClient.h"
#include "support/ServerHarness.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

using lexicontest::Checks;
using lexicontest::HttpTestClient;
using lexicontest::ServerHarness;
using Json = nlohmann::json;

int main() {
  Checks checks;
  lexicontest::HarnessOptions options;
  options.sessions.maxSessions = 64;
  ServerHarness harness(options);
  if (!harness.started()) {
    std::cerr << "harness: " << harness.startupError() << '\n';
    return 1;
  }

  HttpTestClient bootstrap("127.0.0.1", harness.port());
  const auto login =
      bootstrap.post("/api/v1/auth/login",
                     Json{{"username", options.username},
                          {"password", options.password}}
                         .dump());
  checks.expectEqual(login.status, 200, "login for the concurrency test");
  const auto token =
      Json::parse(login.body, nullptr, false).value("token", std::string{});
  bootstrap.setBearerToken(token);
  const int groupId =
      Json::parse(bootstrap.get("/api/v1/groups/default").body, nullptr, false)
          .value("groupId", 0);
  checks.expect(groupId > 0, "the default group exists");

  constexpr int kThreads = 8;
  constexpr int kItemsPerThread = 12;
  std::atomic<int> created{0};
  std::atomic<int> readFailures{0};
  std::atomic<int> writeFailures{0};
  std::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int worker = 0; worker < kThreads; ++worker) {
    workers.emplace_back([&, worker] {
      HttpTestClient client("127.0.0.1", harness.port());
      client.setBearerToken(token);
      for (int index = 0; index < kItemsPerThread; ++index) {
        const auto title = "Concurrent " + std::to_string(worker) + "-" +
                           std::to_string(index);
        const auto response =
            client.post("/api/v1/items",
                        Json{{"item", Json{{"groupId", groupId},
                                           {"title", title},
                                           {"tags", Json::array({"parallel"})},
                                           {"content", "Written by thread " +
                                                           std::to_string(worker)}}}}
                            .dump());
        if (response.status == 201)
          ++created;
        else
          ++writeFailures;
        // Interleave reads, health checks and queries with the writes.
        if (client.get("/api/v1/health").status != 200)
          ++readFailures;
        if (client.post("/api/v1/items/query",
                        Json{{"limit", 10}, {"tagFilter", "parallel"}}.dump())
                .status != 200)
          ++readFailures;
        if (client.get("/api/v1/usage/tags").status != 200)
          ++readFailures;
      }
    });
  }
  for (auto &worker : workers)
    worker.join();

  checks.expectEqual(created.load(), kThreads * kItemsPerThread,
                     "every concurrent write succeeded");
  checks.expectEqual(writeFailures.load(), 0, "no write was rejected");
  checks.expectEqual(readFailures.load(), 0, "no read failed");

  const auto all = bootstrap.post(
      "/api/v1/items/query",
      Json{{"limit", 1000}, {"tagFilter", "parallel"}}.dump());
  const auto body = Json::parse(all.body, nullptr, false);
  checks.expectEqual(body.value("totalCount", 0), kThreads * kItemsPerThread,
                     "the database holds every item exactly once");
  std::set<int> ids;
  std::set<std::string> titles;
  for (const auto &item : body.at("items")) {
    ids.insert(item.value("id", 0));
    titles.insert(item.value("title", std::string{}));
  }
  checks.expectEqual(static_cast<long long>(ids.size()),
                     kThreads * kItemsPerThread, "every item has a unique ID");
  checks.expectEqual(static_cast<long long>(titles.size()),
                     kThreads * kItemsPerThread,
                     "no write clobbered another");

  // Concurrent logins must each get their own session.
  std::vector<std::string> tokens(kThreads);
  std::vector<std::thread> loginWorkers;
  for (int worker = 0; worker < kThreads; ++worker) {
    loginWorkers.emplace_back([&, worker] {
      HttpTestClient client("127.0.0.1", harness.port());
      const auto response =
          client.post("/api/v1/auth/login",
                      Json{{"username", options.username},
                           {"password", options.password}}
                          .dump());
      if (response.status == 200)
        tokens[static_cast<std::size_t>(worker)] =
            Json::parse(response.body, nullptr, false)
                .value("token", std::string{});
    });
  }
  for (auto &worker : loginWorkers)
    worker.join();
  std::set<std::string> uniqueTokens(tokens.begin(), tokens.end());
  checks.expectEqual(static_cast<long long>(uniqueTokens.size()), kThreads,
                     "concurrent logins produce distinct tokens");
  checks.expect(uniqueTokens.find(std::string{}) == uniqueTokens.end(),
                "no concurrent login failed");

  return checks.summarize("server_concurrency");
}
