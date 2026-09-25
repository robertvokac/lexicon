#include "Backup.h"

#include "Exchange.h"
#include "LexiconApplication.h"
#include "ImageValue.h"
#include "SqliteRepository.h"
#include "Utf8Path.h"

#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <memory>
#include <random>
#include <regex>
#include <set>
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
// against its SHA-256 on the way. One already in [previous] is shared with it
// through a hard link where the file system allows it - after that copy, too,
// has been checked: a damaged one is not passed on, the live file is copied
// instead and the damage is reported.
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
      const auto intact = SqliteRepository::fileHasHash(pathToUtf8(earlier), hash);
      if (intact && *intact) {
        fs::create_hard_link(earlier, destination, error);
        if (!error) {
          ++report.blobsLinked;
          continue;
        }
      } else {
        report.damagedInPrevious.push_back(hash);
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

std::string fileSha256(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Cannot read " + pathToUtf8(path));
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1)
    throw std::runtime_error("Cannot initialize SHA-256.");
  std::array<char, 65536> bytes{};
  while (input) {
    input.read(bytes.data(), bytes.size());
    const auto count = input.gcount();
    if (count > 0 && EVP_DigestUpdate(context.get(), bytes.data(), static_cast<std::size_t>(count)) != 1)
      throw std::runtime_error("Cannot hash " + pathToUtf8(path));
  }
  if (!input.eof()) throw std::runtime_error("Cannot read " + pathToUtf8(path));
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int length = 0;
  if (EVP_DigestFinal_ex(context.get(), digest.data(), &length) != 1 || length != 32)
    throw std::runtime_error("Cannot finish SHA-256.");
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(64);
  for (unsigned int i = 0; i < length; ++i) {
    result.push_back(digits[digest[i] >> 4]);
    result.push_back(digits[digest[i] & 15]);
  }
  return result;
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

Result<std::size_t> verifyBackup(const std::string &path) {
  try {
    const fs::path root = utf8Path(path);
    if (fs::symlink_status(root).type() != fs::file_type::directory)
      throw std::runtime_error("Backup path is not a directory.");
    const auto regular = [&](const char *name) {
      const auto file = root / name;
      if (fs::symlink_status(file).type() != fs::file_type::regular)
        throw std::runtime_error(std::string("Missing or non-regular backup file: ") + name);
      return file;
    };
    std::ifstream manifestInput(regular(kManifest), std::ios::binary);
    const auto manifest = nlohmann::json::parse(manifestInput);
    const int version = manifest.value("version", 0);
    if (!manifest.is_object() || manifest.value("format", std::string{}) != "lexicon-backup" ||
        (version != 1 && version != 2) || manifest.value("database", std::string{}) != "lexicon.db" ||
        manifest.value("export", std::string{}) != "lexicon-export.json" ||
        !manifest.contains("blobs") || !manifest["blobs"].is_number_unsigned())
      throw std::runtime_error("Invalid backup manifest.");
    if (version == 2) {
      for (const auto *part : {"database", "export"}) {
        const auto hashKey = std::string(part) + "Sha256";
        if (!manifest.contains(hashKey) || !manifest[hashKey].is_string() ||
            manifest[hashKey].get<std::string>() != fileSha256(regular(std::string_view(part) == "database"
                ? "lexicon.db" : "lexicon-export.json")))
          throw std::runtime_error(std::string("Backup ") + part + " checksum differs from the manifest.");
      }
    }
    std::ifstream exportInput(regular("lexicon-export.json"), std::ios::binary);
    const auto exported = nlohmann::json::parse(exportInput);
    if (!exported.is_object() || exported.value("format", std::string{}) != "lexicon-export" ||
        !exported.contains("items") || !exported["items"].is_array() ||
        !exported.contains("groups") || !exported["groups"].is_array())
      throw std::runtime_error("Invalid dictionary export.");

    const auto database = regular("lexicon.db");
    sqlite3 *raw = nullptr;
    const int opened = sqlite3_open_v2(pathToUtf8(database).c_str(), &raw, SQLITE_OPEN_READONLY, nullptr);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close_v2)> db(raw, sqlite3_close_v2);
    if (opened != SQLITE_OK)
      throw std::runtime_error("Cannot read backup database: " + std::string(raw ? sqlite3_errmsg(raw) : "out of memory"));
    const auto check = [&](const char *sql, const char *expected) {
      sqlite3_stmt *rawStatement = nullptr;
      if (sqlite3_prepare_v2(db.get(), sql, -1, &rawStatement, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db.get()));
      std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(rawStatement, sqlite3_finalize);
      const int result = sqlite3_step(statement.get());
      if (expected) {
        if (result != SQLITE_ROW || !sqlite3_column_text(statement.get(), 0) ||
            std::string(reinterpret_cast<const char *>(sqlite3_column_text(statement.get(), 0))) != expected)
          throw std::runtime_error(std::string(sql) + " failed.");
      } else if (result != SQLITE_DONE) {
        throw std::runtime_error(std::string(sql) + " found invalid references.");
      }
    };
    check("PRAGMA integrity_check;", "ok");
    check("PRAGMA foreign_key_check;", nullptr);

    sqlite3_stmt *rawValues = nullptr;
    const char *sql = "SELECT f.data_type, iv.value FROM item_value iv "
                      "JOIN item_field f ON f.id = iv.item_field_id "
                      "WHERE f.data_type IN (8, 10);";
    if (sqlite3_prepare_v2(db.get(), sql, -1, &rawValues, nullptr) != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(db.get()));
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> values(rawValues, sqlite3_finalize);
    std::set<std::string> hashes;
    int row;
    while ((row = sqlite3_step(values.get())) == SQLITE_ROW) {
      const auto *bytes = sqlite3_column_text(values.get(), 1);
      if (!bytes) throw std::runtime_error("A file reference is null.");
      const auto value = std::string_view(reinterpret_cast<const char *>(bytes),
                                          static_cast<std::size_t>(sqlite3_column_bytes(values.get(), 1)));
      const auto hash = storedFileHash(static_cast<FieldDataType>(sqlite3_column_int(values.get(), 0)), value);
      if (hash.size() != 64 || hash.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw std::runtime_error("An invalid file reference is stored in the backup database.");
      hashes.insert(hash);
    }
    if (row != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db.get()));
    sqlite3_stmt *rawVersion = nullptr;
    if (sqlite3_prepare_v2(db.get(), "PRAGMA user_version;", -1, &rawVersion, nullptr) != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(db.get()));
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> databaseVersion(rawVersion, sqlite3_finalize);
    if (sqlite3_step(databaseVersion.get()) != SQLITE_ROW)
      throw std::runtime_error("Cannot read database schema version.");
    const bool expectedHistory = sqlite3_column_int(databaseVersion.get(), 0) >= 27;
    sqlite3_stmt *rawHistory = nullptr;
    const bool hasHistory = sqlite3_prepare_v2(db.get(), "SELECT snapshot FROM item_history;", -1, &rawHistory,
                                                nullptr) == SQLITE_OK;
    if (expectedHistory && !hasHistory) throw std::runtime_error("Item history is missing from the backup database.");
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> history(rawHistory, sqlite3_finalize);
    while (hasHistory && (row = sqlite3_step(history.get())) == SQLITE_ROW) {
      const auto *bytes = sqlite3_column_text(history.get(), 0);
      if (!bytes) throw std::runtime_error("An item history snapshot is null.");
      const auto snapshot = nlohmann::json::parse(reinterpret_cast<const char *>(bytes));
      if (!snapshot.contains("files") || !snapshot["files"].is_array()) continue;
      for (const auto &file : snapshot["files"]) {
        if (!file.is_string()) throw std::runtime_error("An item history file reference is invalid.");
        const auto hash = file.get<std::string>();
        if (hash.size() != 64 || hash.find_first_not_of("0123456789abcdef") != std::string::npos)
          throw std::runtime_error("An item history file reference is invalid.");
        hashes.insert(hash);
      }
    }
    if (hasHistory && row != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db.get()));
    if (manifest["blobs"].get<std::size_t>() != hashes.size())
      throw std::runtime_error("The manifest file count differs from the database.");
    for (const auto &hash : hashes) {
      const auto file = root / "blobs" / hash.substr(0, 2) / hash.substr(2);
      auto intact = SqliteRepository::fileHasHash(pathToUtf8(file), hash);
      if (!intact || !*intact)
        throw std::runtime_error("Missing or damaged file in backup: " + hash);
    }
    return hashes.size();
  } catch (const std::exception &error) {
    return std::unexpected(failure("Backup verification failed: " + std::string(error.what())));
  }
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
        {"version", 2},
        {"createdAt", std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(now))},
        {"database", "lexicon.db"},
        {"databaseSha256", fileSha256(partial / "lexicon.db")},
        {"export", "lexicon-export.json"},
        {"exportSha256", fileSha256(partial / "lexicon-export.json")},
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
  if (!report.damagedInPrevious.empty())
    text += std::format(" WARNING: {} file(s) in the previous backup no longer match their SHA-256 (first: {}); "
                        "this backup has fresh copies, the older one is damaged.",
                        report.damagedInPrevious.size(), report.damagedInPrevious.front());
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
