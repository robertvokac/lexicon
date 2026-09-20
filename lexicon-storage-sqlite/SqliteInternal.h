#pragma once

#include "Result.h"
#include <sqlite3.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace storage {
struct Failure : std::runtime_error {
  lexicon::Error::Code code;
  explicit Failure(std::string message, lexicon::Error::Code kind = lexicon::Error::Code::Storage)
      : std::runtime_error(std::move(message)), code(kind) {}
};

class Connection {
  sqlite3 *db_ = nullptr;
public:
  Connection() = default;
  ~Connection() { close(); }
  void close() noexcept { if (db_) { sqlite3_close_v2(db_); db_ = nullptr; } }
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  void open(const std::string &path) {
    close();
    int rc = sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
      std::string message = db_ ? sqlite3_errmsg(db_) : "Out of memory";
      close();
      throw Failure("Cannot open database: " + message);
    }
    try { exec("PRAGMA foreign_keys = ON;"); }
    catch (...) { close(); throw; }
  }
  sqlite3 *get() const { if (!db_) throw Failure("Database is not open."); return db_; }
  void exec(const std::string &sql) const {
    char *error = nullptr;
    if (sqlite3_exec(get(), sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
      std::string message = error ? error : sqlite3_errmsg(get());
      sqlite3_free(error);
      throw Failure(message + " SQL: " + sql);
    }
  }
  int lastId() const { return static_cast<int>(sqlite3_last_insert_rowid(get())); }
};

class Statement {
  sqlite3 *db_;
  sqlite3_stmt *stmt_ = nullptr;
  int index_ = 1;
public:
  Statement(const Connection &db, const std::string &sql) : db_(db.get()) {
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
      std::string message = sqlite3_errmsg(db_);
      if (stmt_) sqlite3_finalize(stmt_);
      throw Failure(message);
    }
  }
  ~Statement() { if (stmt_) sqlite3_finalize(stmt_); }
  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;
  Statement &bind(int value) { check(sqlite3_bind_int(stmt_, index_++, value)); return *this; }
  Statement &bind(const std::string &value) {
    check(sqlite3_bind_text(stmt_, index_++, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT)); return *this;
  }
  Statement &bind(std::string_view value) { return bind(std::string(value)); }
  Statement &bind(const char *value) { return bind(std::string(value)); }
  Statement &null() { check(sqlite3_bind_null(stmt_, index_++)); return *this; }
  Statement &nullableId(int id) { return id > 0 ? bind(id) : null(); }
  void reset() { check(sqlite3_reset(stmt_)); check(sqlite3_clear_bindings(stmt_)); index_ = 1; }
  bool step() {
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    throw Failure(sqlite3_errmsg(db_));
  }
  void run() { if (step()) throw Failure("Unexpected row in write statement."); }
  int integer(int column) const { return sqlite3_column_int(stmt_, column); }
  std::string text(int column) const {
    const auto *data = sqlite3_column_text(stmt_, column);
    if (!data) return {};
    return {reinterpret_cast<const char *>(data), static_cast<std::size_t>(sqlite3_column_bytes(stmt_, column))};
  }
  bool isNull(int column) const { return sqlite3_column_type(stmt_, column) == SQLITE_NULL; }
private:
  void check(int rc) { if (rc != SQLITE_OK) throw Failure(sqlite3_errmsg(db_)); }
};

class Transaction {
  const Connection &db_;
  std::string name_;
  bool active_ = true;
public:
  Transaction(const Connection &db, std::string name) : db_(db), name_(std::move(name)) { db_.exec("SAVEPOINT " + name_); }
  ~Transaction() { if (active_) { try { db_.exec("ROLLBACK TO SAVEPOINT " + name_); db_.exec("RELEASE SAVEPOINT " + name_); } catch (...) {} } }
  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;
  void commit() { db_.exec("RELEASE SAVEPOINT " + name_); active_ = false; }
};

inline void run(const Connection &db, const std::string &sql) { Statement(db, sql).run(); }
inline int scalar(const Connection &db, const std::string &sql) { Statement stmt(db, sql); return stmt.step() ? stmt.integer(0) : 0; }
void applyMigrations(const Connection &db);
} // namespace storage
