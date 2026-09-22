// Automatic backups: what a backup holds, how unchanged files are shared,
// rotation, interrupted backups, the scheduler and the command line.
#include "Backup.h"
#include "LexiconApplication.h"
#include "ServerConfig.h"
#include "SqliteRepository.h"
#include "Utf8Path.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

namespace {
namespace fs = std::filesystem;
using namespace std::chrono_literals;
using lexicon::backup::Clock;
int failures = 0;
void check(bool condition, const std::string &message) {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}
struct TemporaryDirectory {
  fs::path path = fs::temp_directory_path() /
                  ("lexicon-backup-test-" + std::to_string(Clock::now().time_since_epoch().count()));
  TemporaryDirectory() { fs::create_directories(path); }
  ~TemporaryDirectory() { std::error_code ignored; fs::remove_all(path, ignored); }
};
std::string readFile(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream text;
  text << input.rdbuf();
  return text.str();
}
Clock::time_point at(const char *utc) {
  std::istringstream input(utc);
  Clock::time_point time;
  input >> std::chrono::parse("%Y-%m-%dT%H:%M:%SZ", time);
  return time;
}
std::string storeFile(lexicon::LexiconApplication &application, const fs::path &directory, const std::string &bytes) {
  const auto source = directory / ("source-" + std::to_string(bytes.size()));
  { std::ofstream(source, std::ios::binary) << bytes; }
  return application.blobs.importFile(lexicon::pathToUtf8(source)).value_or("");
}

void checkBackups() {
  TemporaryDirectory temp;
  const auto database = temp.path / "data" / "lexicon.db";
  fs::create_directories(database.parent_path());
  const auto backupDirectory = temp.path / "backups";
  SqliteRepository repository;
  check(repository.open(lexicon::pathToUtf8(database)).has_value(), "open the database");
  lexicon::LexiconApplication application(repository);
  lexicon::ItemRecord item;
  item.groupId = application.groups.defaultGroupId().value_or(-1);
  item.title = "Monoid";
  item.content = "An associative operation with an identity.";
  check(application.items.createItem(item).has_value(), "create an item");
  const auto first = storeFile(application, temp.path, "the first file");
  check(first.size() == 64, "store a file");

  lexicon::backup::BackupOptions options{lexicon::pathToUtf8(database), lexicon::pathToUtf8(backupDirectory), 2};
  auto made = lexicon::backup::createBackup(options, at("2026-09-20T08:00:00Z"));
  check(made.has_value(), "a backup is made");
  if (!made) { std::cerr << made.error().message << '\n'; return; }
  const fs::path one = lexicon::utf8Path(made->path);
  check(one.filename() == "lexicon-backup-2026-09-20T08-00-00Z", "named by its time");
  check(made->blobsCopied == 1 && made->blobsLinked == 0, "the file is copied");
  check(readFile(one / "blobs" / first.substr(0, 2) / first.substr(2)) == "the first file", "byte for byte");
  const auto manifest = nlohmann::json::parse(readFile(one / "backup.json"));
  check(manifest.value("format", "") == "lexicon-backup" && manifest.value("blobs", 0) == 1, "with a manifest");
  const auto document = nlohmann::json::parse(readFile(one / "lexicon-export.json"));
  check(document.value("format", "") == "lexicon-export" && document.at("items").size() == 1, "and a portable export");
  {
    SqliteRepository copy;
    check(copy.open(lexicon::pathToUtf8(one / "lexicon.db")).has_value(), "the database copy opens");
    lexicon::LexiconApplication restored(copy);
    check(restored.search.findItemId("Monoid", "").has_value(), "and holds the item");
  }

  // The next backup shares the unchanged file instead of copying it again.
  const auto second = storeFile(application, temp.path, "a second, longer file");
  item.title = "Semigroup";
  application.items.createItem(item);
  made = lexicon::backup::createBackup(options, at("2026-09-21T08:00:00Z"));
  check(made && made->blobsCopied == 1 && made->blobsLinked == 1, "an unchanged file is shared");
  if (!made) return;
  const fs::path two = lexicon::utf8Path(made->path);
  std::error_code error;
  check(fs::hard_link_count(two / "blobs" / first.substr(0, 2) / first.substr(2), error) == 2,
        "through a hard link, taking no more space");
  check(readFile(two / "blobs" / second.substr(0, 2) / second.substr(2)) == "a second, longer file", "a new file is copied");
  check(lexicon::backup::createBackup(options, at("2026-09-21T08:00:00Z")).error().message.find("already exists") !=
            std::string::npos,
        "two backups in one second are refused");

  // A backup interrupted long ago is cleared away; one in progress is not.
  const auto stale = backupDirectory / ".partial-lexicon-backup-2026-09-01T08-00-00Z-dead";
  const auto running = backupDirectory / ".partial-lexicon-backup-2026-09-22T07-59-59Z-busy";
  fs::create_directories(stale);
  fs::create_directories(running);
  fs::create_directories(backupDirectory / "lexicon-backup-2026-09-19T08-00-00Z"); // No manifest: incomplete.
  fs::last_write_time(stale, fs::file_time_type::clock::now() - 48h);
  made = lexicon::backup::createBackup(options, at("2026-09-22T08:00:00Z"));
  check(made.has_value(), "a third backup is made");
  check(!fs::exists(stale) && fs::exists(running), "a stale partial backup is removed, a recent one kept");
  check(made && made->removed == std::vector<std::string>{"lexicon-backup-2026-09-20T08-00-00Z"},
        "only the newest backups are kept");
  const auto listed = lexicon::backup::listBackups(options.directory);
  check(listed.size() == 2 && listed.front().name == "lexicon-backup-2026-09-22T08-00-00Z" &&
            listed.back().name == "lexicon-backup-2026-09-21T08-00-00Z",
        "the list holds complete backups, the newest first");
  check(fs::exists(two / "blobs" / first.substr(0, 2) / first.substr(2)),
        "removing an old backup leaves the shared file in the newer one");
  check(!repository.snapshotTo(lexicon::pathToUtf8(two / "lexicon.db")).has_value(),
        "a snapshot never overwrites a file");
  std::cout << lexicon::backup::describe(*made) << '\n';
}

void checkScheduler() {
  TemporaryDirectory temp;
  const auto database = temp.path / "lexicon.db";
  { SqliteRepository repository; repository.open(lexicon::pathToUtf8(database)); }
  const auto directory = lexicon::pathToUtf8(temp.path / "backups");
  std::vector<std::string> log;
  std::mutex logMutex;
  {
    lexicon::backup::BackupScheduler scheduler({lexicon::pathToUtf8(database), directory, 5}, 300ms,
                                               [&](const std::string &line) {
                                                 std::lock_guard lock(logMutex);
                                                 log.push_back(line);
                                               },
                                               300ms);
    scheduler.start();
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (lexicon::backup::listBackups(directory).size() < 2 && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(50ms);
    const auto started = std::chrono::steady_clock::now();
    scheduler.stop();
    check(std::chrono::steady_clock::now() - started < 3s, "stopping does not wait for the next backup");
  }
  check(lexicon::backup::listBackups(directory).size() >= 2, "the scheduler backs up at once, then again");
  check(!log.empty() && log.front().starts_with("Backup written to "), "and says so");

  // A recent backup: the next one waits for the interval.
  const auto before = lexicon::backup::listBackups(directory).size();
  {
    lexicon::backup::BackupScheduler scheduler({lexicon::pathToUtf8(database), directory, 5}, 1h, [](const std::string &) {});
    scheduler.start();
    std::this_thread::sleep_for(400ms);
  }
  check(lexicon::backup::listBackups(directory).size() == before, "no backup before the interval has passed");
}

void checkCommandLine() {
  using lexicon::http::parseCommandLine;
  auto backup = parseCommandLine({"backup", "--backup-dir", "/srv/backups", "--backup-keep", "30"});
  check(backup && backup->command == lexicon::http::Command::Backup && backup->config.backupKeep == 30,
        "backup takes a directory and a count");
  check(!parseCommandLine({"backup"}), "backup needs --backup-dir");
  auto serve = parseCommandLine({"--backup-dir", "/srv/backups", "--backup-interval", "6"});
  check(serve && serve->config.backupDirectory == "/srv/backups" && serve->config.backupIntervalHours == 6,
        "the server takes a directory and an interval");
  check(!parseCommandLine({"--backup-interval", "0"}), "the interval is at least an hour");
  check(!parseCommandLine({"--backup-keep", "0"}), "at least one backup is kept");
  lexicon::http::ServerConfig config;
  config.databasePath = "/srv/lexicon/lexicon.db";
  config.backupDirectory = "/srv/lexicon/blobs/backups";
  check(!lexicon::http::validate(config), "backups never go inside the Blob directory");
  config.backupDirectory = "/srv/lexicon/backups";
  check(lexicon::http::validate(config).has_value(), "beside it they may");
}
} // namespace

int main() {
  checkBackups();
  checkScheduler();
  checkCommandLine();
  if (failures == 0) std::cout << "backup: all checks passed\n";
  return failures == 0 ? 0 : 1;
}
