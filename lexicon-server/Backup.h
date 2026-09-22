#pragma once
// Automatic backups for LexiconServer: every backup is a directory of its own
// holding a consistent copy of the database, a portable export and the Blob
// files, so any one of them restores the whole dictionary.
#include "Result.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lexicon::backup {
using Clock = std::chrono::system_clock;

struct BackupOptions {
  std::string databasePath;
  std::string directory;
  // Complete backups kept; older ones are removed after a new one succeeds.
  int keep = 14;
  // For tests: runs right after the database copy, to change the live data
  // while the rest of the backup is made.
  std::function<void()> afterDatabaseCopy;
};

struct BackupReport {
  std::string path;
  // Blob files copied, and those shared with the previous backup through a
  // hard link because they cannot have changed.
  std::size_t blobsCopied = 0;
  std::size_t blobsLinked = 0;
  std::uintmax_t bytes = 0;
  std::vector<std::string> removed;
};

struct BackupEntry {
  std::string path;
  std::string name;
  Clock::time_point createdAt;
};

// "lexicon-backup-2026-09-22T08-00-00Z": sortable, and valid on every file system.
std::string backupName(Clock::time_point time);
// The complete backups in [directory], the newest first.
std::vector<BackupEntry> listBackups(const std::string &directory);
// Makes one backup now: a copy of the database, then the export and the list
// of files taken from that copy - never from the live database, which may
// have changed meanwhile - and every file that copy refers to, each checked
// against its SHA-256. A missing or damaged file fails the backup. It is
// built under a temporary name and renamed when complete, so an interrupted
// or failed backup never looks like one.
Result<BackupReport> createBackup(const BackupOptions &options, Clock::time_point now = Clock::now());
// "Backup ... written: 3 files copied, 12 shared, 4.2 MB; removed 1 old backup."
std::string describe(const BackupReport &report);

// Runs createBackup in a background thread: first when the newest backup is
// older than [interval] (at once when there is none), then every [interval].
// A failed backup is tried again after [retry].
class BackupScheduler {
public:
  using Log = std::function<void(const std::string &)>;
  BackupScheduler(BackupOptions options, std::chrono::milliseconds interval, Log log,
                  std::chrono::milliseconds retry = std::chrono::hours(1));
  ~BackupScheduler();
  BackupScheduler(const BackupScheduler &) = delete;
  BackupScheduler &operator=(const BackupScheduler &) = delete;
  void start();
  // Waits for a backup in progress to finish.
  void stop();

private:
  void run();

  BackupOptions options_;
  std::chrono::milliseconds interval_;
  std::chrono::milliseconds retry_;
  Log log_;
  std::mutex mutex_;
  std::condition_variable wake_;
  bool stopping_ = false;
  std::thread thread_;
};
} // namespace lexicon::backup
