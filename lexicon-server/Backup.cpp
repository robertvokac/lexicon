#include "Backup.h"

#include "Exchange.h"
#include "LexiconApplication.h"
#include "SqliteRepository.h"
#include "Utf8Path.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <random>
#include <regex>
#include <system_error>

namespace lexicon::backup {
namespace fs = std::filesystem;
namespace {
constexpr std::string_view kPrefix = "lexicon-backup-";
constexpr std::string_view kPartialPrefix = ".partial-lexicon-backup-";
constexpr const char *kManifest = "backup.json";
// A partial backup this old was left by a crash, not by a backup running now.
constexpr auto kStalePartial = std::chrono::hours(24);

Error failure(std::string message) { return Error{Error::Code::Storage, std::move(message)}; }

std::optional<Clock::time_point> parseName(const std::string &name) {
  static const std::regex pattern(R"(^lexicon-backup-(\d{4})-(\d{2})-(\d{2})T(\d{2})-(\d{2})-(\d{2})Z$)");
  std::smatch match;
  if (!std::regex_match(name, match, pattern))
    return std::nullopt;
  const auto number = [&](int index) { return std::stoi(match.str(index)); };
  const std::chrono::year_month_day day{std::chrono::year(number(1)), std::chrono::month(number(2)),
                                        std::chrono::day(number(3))};
  if (!day.ok())
    return std::nullopt;
  return std::chrono::sys_days(day) + std::chrono::hours(number(4)) + std::chrono::minutes(number(5)) +
         std::chrono::seconds(number(6));
}

std::string randomSuffix() {
  std::random_device device;
  return std::format("{:08x}", device());
}

// Copies the files [hashes] name from the live Blob store, each checked
// against its SHA-256 on the way. One unchanged since [previous] is shared
// with it through a hard link where the file system allows it.
void copyBlobs(SqliteRepository &live, const std::vector<std::string> &hashes, const fs::path &target,
               const fs::path &previous, BackupReport &report) {
  for (const auto &hash : hashes) {
    const auto prefix = hash.substr(0, 2);
    const auto name = hash.substr(2);
    fs::create_directories(target / prefix);
    const auto destination = target / prefix / name;
    std::error_code error;
    const auto earlier = previous.empty() ? fs::path() : previous / prefix / name;
    if (!earlier.empty() && fs::symlink_status(earlier, error).type() == fs::file_type::regular) {
      fs::create_hard_link(earlier, destination, error);
      if (!error) {
        ++report.blobsLinked;
        continue;
      }
    }
    if (auto copied = live.exportBlob(hash, pathToUtf8(destination)); !copied)
      throw std::runtime_error("File " + hash + " cannot be backed up: " + copied.error().message);
    ++report.blobsCopied;
    report.bytes += fs::file_size(destination);
  }
}

void removeStalePartials(const fs::path &directory, Clock::time_point now) {
  std::error_code error;
  for (const auto &entry : fs::directory_iterator(directory, error)) {
    const auto name = pathToUtf8(entry.path().filename());
    if (!name.starts_with(kPartialPrefix))
      continue;
    const auto written = std::chrono::clock_cast<Clock>(fs::last_write_time(entry.path(), error));
    if (!error && now - written > kStalePartial)
      fs::remove_all(entry.path(), error);
  }
}

std::string sizeText(std::uintmax_t bytes) {
  if (bytes < 1024)
    return std::format("{} B", bytes);
  if (bytes < 1024 * 1024)
    return std::format("{:.1f} KB", static_cast<double>(bytes) / 1024);
  return std::format("{:.1f} MB", static_cast<double>(bytes) / (1024 * 1024));
}
} // namespace

std::string backupName(Clock::time_point time) {
  return std::format("{}{:%Y-%m-%dT%H-%M-%SZ}", kPrefix, std::chrono::floor<std::chrono::seconds>(time));
}

std::vector<BackupEntry> listBackups(const std::string &directory) {
  std::vector<BackupEntry> backups;
  std::error_code error;
  for (const auto &entry : fs::directory_iterator(utf8Path(directory), error)) {
    const auto name = pathToUtf8(entry.path().filename());
    const auto createdAt = parseName(name);
    if (!createdAt || entry.symlink_status().type() != fs::file_type::directory ||
        !fs::exists(entry.path() / kManifest, error))
      continue;
    backups.push_back({pathToUtf8(entry.path()), name, *createdAt});
  }
  std::sort(backups.begin(), backups.end(), [](const auto &a, const auto &b) { return a.name > b.name; });
  return backups;
}

Result<BackupReport> createBackup(const BackupOptions &options, Clock::time_point now) {
  if (options.keep < 1)
    return std::unexpected(Error{Error::Code::Validation, "Keep at least one backup."});
  BackupReport report;
  const fs::path directory = utf8Path(options.directory);
  const auto name = backupName(now);
  const fs::path final = directory / utf8Path(name);
  const fs::path partial = directory / utf8Path(std::string(kPartialPrefix) + name + "-" + randomSuffix());
  try {
    fs::create_directories(directory);
    removeStalePartials(directory, now);
    if (fs::exists(final))
      return std::unexpected(failure("A backup named " + name + " already exists."));
    const auto earlier = listBackups(options.directory);
    fs::create_directory(partial);

    // The moment the backup stands for: one consistent copy of the database.
    SqliteRepository live;
    if (auto opened = live.open(options.databasePath); !opened)
      throw std::runtime_error("Cannot open the database: " + opened.error().message);
    if (auto copied = live.snapshotTo(pathToUtf8(partial / "lexicon.db")); !copied)
      throw std::runtime_error("Cannot copy the database: " + copied.error().message);
    if (options.afterDatabaseCopy)
      options.afterDatabaseCopy();

    // Everything else comes from that copy, whatever happens to the live
    // database meanwhile.
    std::vector<std::string> required;
    {
      SqliteRepository copy;
      if (auto opened = copy.open(pathToUtf8(partial / "lexicon.db")); !opened)
        throw std::runtime_error("Cannot open the database copy: " + opened.error().message);
      LexiconApplication application(copy);
      // Readable by any Lexicon, whatever the schema; the files are beside it.
      auto document = exchange::exportDocument(application, false);
      if (!document)
        throw std::runtime_error("Cannot export: " + document.error().message);
      std::ofstream output(partial / "lexicon-export.json", std::ios::binary | std::ios::trunc);
      output << *document;
      if (!output.flush())
        throw std::runtime_error("Cannot write the export.");
      auto hashes = copy.referencedBlobHashes();
      if (!hashes)
        throw std::runtime_error("Cannot list the files: " + hashes.error().message);
      required = std::move(*hashes);
    }
    report.bytes += fs::file_size(partial / "lexicon.db") + fs::file_size(partial / "lexicon-export.json");

    // The files the copy refers to - no more (unreferenced files are not part
    // of the dictionary) and no fewer (a missing one fails the backup).
    const fs::path previousBlobs = earlier.empty() ? fs::path() : utf8Path(earlier.front().path) / "blobs";
    copyBlobs(live, required, partial / "blobs", previousBlobs, report);

    const nlohmann::json manifest{
        {"format", "lexicon-backup"},
        {"version", 1},
        {"createdAt", std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(now))},
        {"database", "lexicon.db"},
        {"export", "lexicon-export.json"},
        {"blobs", report.blobsCopied + report.blobsLinked}};
    {
      std::ofstream output(partial / kManifest, std::ios::binary | std::ios::trunc);
      output << manifest.dump(2) << '\n';
      if (!output.flush())
        throw std::runtime_error("Cannot write the backup manifest.");
    }
    fs::rename(partial, final);
    report.path = pathToUtf8(final);

    // Only now, with a new complete backup in place, do old ones go.
    const auto all = listBackups(options.directory);
    for (std::size_t index = static_cast<std::size_t>(options.keep); index < all.size(); ++index) {
      std::error_code error;
      fs::remove_all(utf8Path(all[index].path), error);
      if (!error)
        report.removed.push_back(all[index].name);
    }
    return report;
  } catch (const std::exception &error) {
    std::error_code ignored;
    fs::remove_all(partial, ignored);
    return std::unexpected(failure(std::string("Backup failed: ") + error.what()));
  }
}

std::string describe(const BackupReport &report) {
  const auto files = report.blobsCopied + report.blobsLinked;
  std::string text = std::format("Backup written to {} ({} written", report.path, sizeText(report.bytes));
  if (files > 0)
    text += std::format("; {} file(s), {} unchanged since the last backup", files, report.blobsLinked);
  text += ").";
  if (!report.removed.empty())
    text += std::format(" Removed {} old backup(s).", report.removed.size());
  return text;
}

BackupScheduler::BackupScheduler(BackupOptions options, std::chrono::milliseconds interval, Log log,
                                 std::chrono::milliseconds retry)
    : options_(std::move(options)), interval_(interval), retry_(std::min(retry, interval)), log_(std::move(log)) {}

BackupScheduler::~BackupScheduler() { stop(); }

void BackupScheduler::start() {
  if (thread_.joinable())
    return;
  stopping_ = false;
  thread_ = std::thread([this] { run(); });
}

void BackupScheduler::stop() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_all();
  if (thread_.joinable())
    thread_.join();
}

void BackupScheduler::run() {
  const auto backups = listBackups(options_.directory);
  // The system clock decides "older than the interval"; the wait itself uses
  // the steady clock, so a clock change cannot make it sleep for days.
  auto due = std::chrono::steady_clock::now();
  if (!backups.empty()) {
    const auto age = Clock::now() - backups.front().createdAt;
    if (age < interval_)
      due += std::chrono::duration_cast<std::chrono::steady_clock::duration>(interval_ - age);
  }
  std::unique_lock lock(mutex_);
  while (!wake_.wait_until(lock, due, [this] { return stopping_; })) {
    lock.unlock();
    auto made = createBackup(options_);
    if (made)
      log_(describe(*made));
    else
      log_(made.error().message + " Trying again later.");
    lock.lock();
    due = std::chrono::steady_clock::now() + (made ? interval_ : retry_);
  }
}
} // namespace lexicon::backup
