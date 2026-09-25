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
#include <memory>
#include <set>
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

struct Dictionary {
  TemporaryDirectory temp;
  fs::path database = temp.path / "data" / "lexicon.db";
  SqliteRepository repository;
  std::unique_ptr<lexicon::LexiconApplication> application;
  int group = -1;
  int fileField = -1;
  int typeId = -1;
  Dictionary() {
    fs::create_directories(database.parent_path());
    check(repository.open(lexicon::pathToUtf8(database)).has_value(), "open the database");
    application = std::make_unique<lexicon::LexiconApplication>(repository);
    group = application->groups.defaultGroupId().value_or(-1);
    lexicon::ItemTypeRecord type;
    type.name = "Document";
    type.groupId = group;
    application->types.upsertItemType(type);
    for (const auto &known : application->types.loadItemTypes(group).value_or(std::vector<lexicon::ItemTypeRecord>{}))
      if (known.name == "Document") typeId = known.id;
    lexicon::ItemFieldRecord field;
    field.itemTypeId = typeId;
    field.name = "File";
    field.dataType = lexicon::FieldDataType::Blob;
    application->types.upsertItemField(field);
    fileField = application->types.loadItemFields(typeId).value_or(std::vector<lexicon::ItemFieldRecord>{}).at(0).id;
  }
  // An item whose File field holds [bytes]; returns the file's SHA-256.
  std::string itemWithFile(const std::string &title, const std::string &bytes) {
    const auto hash = storeFile(*application, temp.path, bytes);
    lexicon::ItemRecord item;
    item.groupId = group;
    item.itemTypeId = typeId;
    item.title = title;
    item.content = title + " notes.";
    item.fieldValues[fileField] = hash;
    check(application->items.createItem(item).has_value(), "create " + title);
    return hash;
  }
  fs::path blobFile(const std::string &hash) const {
    return lexicon::utf8Path(SqliteRepository::blobDirectory(lexicon::pathToUtf8(database))) / hash.substr(0, 2) /
           hash.substr(2);
  }
};

void checkBackups() {
  Dictionary data;
  auto &application = *data.application;
  const auto first = data.itemWithFile("Monoid", "the first file");
  const auto orphan = storeFile(application, data.temp.path, "a file no value refers to");
  // A card is a row of the database, so the copy holds it and so does the
  // export taken from the copy.
  const int monoid = application.search.findItemId("Monoid", "").value_or(-1);
  const auto card = application.cards.createCard(monoid, "Co je monoid?", "Pologrupa s jednotkou.");
  check(card.has_value() && application.cards.recordAttempt(card->id, true).has_value(), "a card, answered once");
  check(first.size() == 64 && orphan.size() == 64, "store the files");
  const auto backupDirectory = data.temp.path / "backups";

  lexicon::backup::BackupOptions options{lexicon::pathToUtf8(data.database), lexicon::pathToUtf8(backupDirectory), 2, {}};
  auto made = lexicon::backup::createBackup(options, at("2026-09-20T08:00:00Z"));
  check(made.has_value(), "a backup is made");
  if (!made) { std::cerr << made.error().message << '\n'; return; }
  const fs::path one = lexicon::utf8Path(made->path);
  check(one.filename() == "lexicon-backup-2026-09-20T08-00-00Z", "named by its time");
  check(made->blobsCopied == 1 && made->blobsLinked == 0, "the referenced file is copied");
  auto verified = lexicon::backup::verifyBackup(made->path);
  check(verified && *verified == 1, "the completed backup verifies without changing it");
  const auto originalExport = readFile(one / "lexicon-export.json");
  { std::ofstream(one / "lexicon-export.json", std::ios::binary | std::ios::trunc) << "{}"; }
  check(!lexicon::backup::verifyBackup(made->path), "verification detects a changed export");
  { std::ofstream(one / "lexicon-export.json", std::ios::binary | std::ios::trunc) << originalExport; }
  check(readFile(one / "blobs" / first.substr(0, 2) / first.substr(2)) == "the first file", "byte for byte");
  check(!fs::exists(one / "blobs" / orphan.substr(0, 2) / orphan.substr(2)), "a file nothing refers to is left out");
  const auto manifest = nlohmann::json::parse(readFile(one / "backup.json"));
  check(manifest.value("format", "") == "lexicon-backup" && manifest.value("version", 0) == 2 &&
            manifest.value("blobs", 0) == 1, "with a checksummed manifest");
  const auto document = nlohmann::json::parse(readFile(one / "lexicon-export.json"));
  check(document.value("format", "") == "lexicon-export" && document.at("items").size() == 1, "and a portable export");
  check(document.contains("cards") && document.at("cards").size() == 1 &&
            document.at("cards").at(0).value("question", "") == "Co je monoid?" &&
            document.at("cards").at(0).value("successCount", 0) == 1,
        "whose cards come with their statistics");
  std::set<std::string> files;
  for (const auto &entry : fs::directory_iterator(one)) files.insert(entry.path().filename().string());
  check(files == std::set<std::string>{"backup.json", "blobs", "lexicon-export.json", "lexicon.db"},
        "and nothing else, no journal left beside the copy");
  {
    SqliteRepository copy;
    check(copy.open(lexicon::pathToUtf8(one / "lexicon.db")).has_value(), "the database copy opens");
    lexicon::LexiconApplication restored(copy);
    check(restored.search.findItemId("Monoid", "").has_value(), "and holds the item");
    const auto cards = restored.cards.loadCards(restored.search.findItemId("Monoid", "").value_or(-1));
    check(cards && cards->size() == 1 && cards->front().successCount == 1 && !cards->front().lastAttempt.empty(),
          "and its card");
  }

  // The next backup shares the unchanged file instead of copying it again.
  const auto second = data.itemWithFile("Semigroup", "a second, longer file");
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
  // Their ages count from the backup's time, not from today.
  const auto third = at("2026-09-22T08:00:00Z");
  const auto stale = backupDirectory / ".partial-lexicon-backup-2026-09-01T08-00-00Z-dead";
  const auto running = backupDirectory / ".partial-lexicon-backup-2026-09-22T07-59-59Z-busy";
  fs::create_directories(stale);
  fs::create_directories(running);
  fs::create_directories(backupDirectory / "lexicon-backup-2026-09-19T08-00-00Z"); // No manifest: incomplete.
  fs::last_write_time(stale, std::chrono::clock_cast<fs::file_time_type::clock>(third - 48h));
  fs::last_write_time(running, std::chrono::clock_cast<fs::file_time_type::clock>(third - 1s));
  made = lexicon::backup::createBackup(options, third);
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
  check(!data.repository.snapshotTo(lexicon::pathToUtf8(two / "lexicon.db")).has_value(),
        "a snapshot never overwrites a file");
  std::cout << lexicon::backup::describe(*made) << '\n';
}

// While a backup runs, the live dictionary goes on changing: in this server,
// in the desktop client on the same file, or in the desktop's Blob cleanup.
void checkChangesDuringABackup() {
  Dictionary data;
  auto &application = *data.application;
  const auto kept = data.itemWithFile("Monoid", "the monoid file");
  const auto backups = lexicon::pathToUtf8(data.temp.path / "backups");

  // An item changed and another deleted right after the database copy: the
  // export and the files still describe the moment of the copy.
  const auto semigroup = data.itemWithFile("Semigroup", "the semigroup file");
  lexicon::backup::BackupOptions options{lexicon::pathToUtf8(data.database), backups, 5, [&] {
    SqliteRepository other;
    other.open(lexicon::pathToUtf8(data.database));
    lexicon::LexiconApplication elsewhere(other);
    auto monoid = elsewhere.items.loadItem(elsewhere.search.findItemId("Monoid", "").value_or(-1));
    if (monoid) {
      monoid->content = "Changed after the copy.";
      elsewhere.items.saveItem(*monoid);
    }
    elsewhere.items.deleteItem(elsewhere.search.findItemId("Semigroup", "").value_or(-1));
  }};
  auto made = lexicon::backup::createBackup(options, at("2026-09-20T08:00:00Z"));
  check(made.has_value(), "a backup made while the data changes succeeds");
  if (!made) { std::cerr << made.error().message << '\n'; return; }
  const fs::path backup = lexicon::utf8Path(made->path);
  const auto document = nlohmann::json::parse(readFile(backup / "lexicon-export.json"));
  std::set<std::string> titles;
  std::string monoidContent;
  for (const auto &item : document.at("items")) {
    titles.insert(item.value("title", ""));
    if (item.value("title", "") == "Monoid") monoidContent = item.value("content", "");
  }
  check(titles == std::set<std::string>{"Monoid", "Semigroup"}, "the export is the database copy, not the live data");
  check(monoidContent == "Monoid notes.", "with the content as it was at the copy");
  check(fs::exists(backup / "blobs" / semigroup.substr(0, 2) / semigroup.substr(2)),
        "a file the copy refers to is backed up though the live item is gone");
  check(fs::exists(backup / "blobs" / kept.substr(0, 2) / kept.substr(2)), "and so is the other");

  // A file the copy refers to vanishes before it is copied (the desktop's
  // Blob cleanup, say): no backup is better than an incomplete one.
  const auto doomed = data.itemWithFile("Group", "the group file");
  options.afterDatabaseCopy = [&] { fs::remove(data.blobFile(doomed)); };
  auto failed = lexicon::backup::createBackup(options, at("2026-09-21T08:00:00Z"));
  check(!failed.has_value() && failed.error().message.find(doomed) != std::string::npos,
        "a missing file fails the backup and is named");
  check(lexicon::backup::listBackups(backups).size() == 1, "and leaves no backup that looks complete");
  std::size_t partials = 0;
  for (const auto &entry : fs::directory_iterator(lexicon::utf8Path(backups)))
    if (entry.path().filename().string().starts_with(".partial-")) ++partials;
  check(partials == 0, "nor its half-made directory");

  // A deleted item's file remains referenced by Trash. Restore this test
  // fixture's vanished bytes before testing damage in another file.
  application.items.deleteItem(application.search.findItemId("Group", "").value_or(-1));
  { std::ofstream(data.blobFile(doomed), std::ios::binary) << "the group file"; }
  const auto damaged = data.itemWithFile("Ring", "the ring file");
  options.afterDatabaseCopy = [&] {
    // Blob files are stored read-only; this one rots anyway.
    fs::permissions(data.blobFile(damaged), fs::perms::owner_write, fs::perm_options::add);
    std::ofstream(data.blobFile(damaged), std::ios::binary | std::ios::trunc) << "garbage";
  };
  failed = lexicon::backup::createBackup(options, at("2026-09-22T08:00:00Z"));
  check(!failed.has_value() && failed.error().message.find(damaged) != std::string::npos,
        "a file whose bytes no longer match its SHA-256 fails the backup");
}

// A backup shares unchanged files with the previous one; a file that has
// rotted there since must not be passed on.
void checkDamageInThePreviousBackup() {
  Dictionary data;
  const auto hash = data.itemWithFile("Monoid", "the monoid file");
  const auto backups = lexicon::pathToUtf8(data.temp.path / "backups");
  lexicon::backup::BackupOptions options{lexicon::pathToUtf8(data.database), backups, 5, {}};
  const auto rot = [](const fs::path &file, const char *bytes) {
    fs::permissions(file, fs::perms::owner_write, fs::perm_options::add);
    std::ofstream(file, std::ios::binary | std::ios::trunc) << bytes;
  };
  const auto inBackup = [&](const std::string &path) {
    return lexicon::utf8Path(path) / "blobs" / hash.substr(0, 2) / hash.substr(2);
  };

  auto first = lexicon::backup::createBackup(options, at("2026-09-20T08:00:00Z"));
  check(first.has_value(), "the first backup is made");
  if (!first) return;
  rot(inBackup(first->path), "rotten");

  auto second = lexicon::backup::createBackup(options, at("2026-09-21T08:00:00Z"));
  check(second.has_value(), "the next backup is made despite the damage");
  if (!second) { std::cerr << second.error().message << '\n'; return; }
  std::error_code error;
  check(readFile(inBackup(second->path)) == "the monoid file", "with a sound copy of the file from the live store");
  check(fs::hard_link_count(inBackup(second->path), error) == 1, "not a link to the damaged one");
  check(second->blobsCopied == 1 && second->blobsLinked == 0, "counted as copied");
  check(second->damagedInPrevious == std::vector<std::string>{hash}, "and the damage is reported");
  check(lexicon::backup::describe(*second).find("WARNING") != std::string::npos, "loudly");

  // The live file rots, but the last backup's copy is sound: that one is used.
  rot(data.blobFile(hash), "rotten too");
  auto third = lexicon::backup::createBackup(options, at("2026-09-22T08:00:00Z"));
  check(third && third->blobsLinked == 1 && third->damagedInPrevious.empty(),
        "a sound copy in the previous backup is shared though the live file rotted");
  check(third && readFile(inBackup(third->path)) == "the monoid file", "and the backup holds the right bytes");

  // Both damaged: no backup at all.
  if (third) rot(inBackup(third->path), "rotten as well");
  auto fourth = lexicon::backup::createBackup(options, at("2026-09-23T08:00:00Z"));
  check(!fourth.has_value() && fourth.error().message.find(hash) != std::string::npos,
        "with no sound copy anywhere the backup fails and names the file");
  check(lexicon::backup::listBackups(backups).size() == 3, "and adds nothing that looks complete");
  if (third)
    check(!lexicon::backup::verifyBackup(third->path).has_value(), "verification detects a damaged referenced file");
}

void checkScheduler() {
  TemporaryDirectory temp;
  const auto database = temp.path / "lexicon.db";
  { SqliteRepository repository; repository.open(lexicon::pathToUtf8(database)); }
  const auto directory = lexicon::pathToUtf8(temp.path / "backups");
  std::vector<std::string> log;
  std::mutex logMutex;
  {
    lexicon::backup::BackupScheduler scheduler({lexicon::pathToUtf8(database), directory, 5, {}}, 300ms,
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
    lexicon::backup::BackupScheduler scheduler({lexicon::pathToUtf8(database), directory, 5, {}}, 1h, [](const std::string &) {});
    scheduler.start();
    std::this_thread::sleep_for(400ms);
  }
  check(lexicon::backup::listBackups(directory).size() == before, "no backup before the interval has passed");
}

void checkDeletedItemFiles() {
  Dictionary data;
  const auto hash = data.itemWithFile("Gone", "a file retained by Trash");
  const int id = data.application->search.findItemId("Gone", "").value_or(-1);
  check(data.application->items.deleteItem(id).has_value(), "delete the file-bearing item");
  auto scan = data.application->blobs.scanStorage(lexicon::BlobScanDepth::FullIntegrity);
  check(scan.has_value(), "scan storage after the deletion");
  if (scan) {
    auto collected = data.application->blobs.collectUnusedBlobs(*scan);
    check(collected.has_value() && fs::exists(data.blobFile(hash)),
          "Blob cleanup keeps a file referenced by Trash");
  }
  lexicon::backup::BackupOptions options{lexicon::pathToUtf8(data.database),
      lexicon::pathToUtf8(data.temp.path / "backups"), 2, {}};
  auto made = lexicon::backup::createBackup(options, at("2026-09-22T08:00:00Z"));
  check(made.has_value(), "backup of a deleted file-bearing item succeeds");
  if (!made) return;
  auto verified = lexicon::backup::verifyBackup(made->path);
  check(verified && *verified == 1, "backup includes the file retained by Trash");
  const auto trash = data.application->items.loadTrash();
  if (trash && !trash->empty()) {
    const auto restored = data.application->items.restoreItemHistory(trash->front().id);
    const auto item = restored ? data.application->items.loadItem(*restored) : lexicon::Result<lexicon::ItemRecord>{};
    check(item && item->fieldValues.at(data.fileField) == hash,
          "restoring the deleted item reuses its intact file");
  }
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
  checkChangesDuringABackup();
  checkDamageInThePreviousBackup();
  checkDeletedItemFiles();
  checkScheduler();
  checkCommandLine();
  if (failures == 0) std::cout << "backup: all checks passed\n";
  return failures == 0 ? 0 : 1;
}
