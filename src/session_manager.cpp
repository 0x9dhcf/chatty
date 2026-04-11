#include "session_manager.hpp"
#include "paths.hpp"
#include <agt/session.hpp>
#include <format>
#include <random>
#include <sqlite3.h>
#include <stdexcept>

static std::string generate_uuid() {
  static std::mt19937_64 rng(std::random_device{}());
  std::uniform_int_distribution<uint64_t> dist;
  uint64_t hi = dist(rng);
  uint64_t lo = dist(rng);
  // Set version 4 bits
  hi = (hi & ~(0xFULL << 12)) | (0x4ULL << 12);
  // Set variant 1 bits
  lo = (lo & ~(0x3ULL << 62)) | (0x2ULL << 62);
  return std::format("{:08x}-{:04x}-{:04x}-{:04x}-{:012x}", static_cast<uint32_t>(hi >> 32),
                     static_cast<uint16_t>((hi >> 16) & 0xFFFF), static_cast<uint16_t>(hi & 0xFFFF),
                     static_cast<uint16_t>((lo >> 48) & 0xFFFF), lo & 0xFFFFFFFFFFFFULL);
}

static void exec(sqlite3* db, const char* sql) {
  char* err = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    std::string msg = err ? err : "unknown error";
    sqlite3_free(err);
    throw std::runtime_error("sqlite: " + msg);
  }
}

SessionManager::SessionManager() {
  auto base = states_dir();
  sessions_dir_ = base / "sessions";
  std::filesystem::create_directories(sessions_dir_);

  auto db_path = base / "sessions.db";
  if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK)
    throw std::runtime_error("Cannot open session index: " + db_path.string());

  exec(db_, "PRAGMA journal_mode=WAL");
  exec(db_, "CREATE TABLE IF NOT EXISTS sessions ("
            "  uuid            TEXT PRIMARY KEY,"
            "  name            TEXT NOT NULL,"
            "  provider        TEXT NOT NULL DEFAULT '',"
            "  model           TEXT NOT NULL DEFAULT '',"
            "  thinking_effort TEXT NOT NULL DEFAULT '',"
            "  created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now')),"
            "  updated_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now'))"
            ")");
}

SessionManager::~SessionManager() {
  if (db_)
    sqlite3_close(db_);
}

SessionInfo SessionManager::create(const std::string& name, const ChattySettings& cfg) {
  auto uuid = generate_uuid();
  auto provider = std::string(agt::provider_to_string(cfg.provider));

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_,
                     "INSERT INTO sessions (uuid, name, provider, model, thinking_effort) "
                     "VALUES (?, ?, ?, ?, ?)",
                     -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, provider.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, cfg.model.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, cfg.thinking_effort.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error("Failed to create session");
  }
  sqlite3_finalize(stmt);

  // Read back to get timestamps
  SessionInfo info;
  info.uuid = uuid;
  info.name = name;
  info.provider = provider;
  info.model = cfg.model;
  info.thinking_effort = cfg.thinking_effort;

  sqlite3_stmt* q = nullptr;
  sqlite3_prepare_v2(db_, "SELECT created_at, updated_at FROM sessions WHERE uuid = ?", -1, &q,
                     nullptr);
  sqlite3_bind_text(q, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(q) == SQLITE_ROW) {
    info.created_at = reinterpret_cast<const char*>(sqlite3_column_text(q, 0));
    info.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(q, 1));
  }
  sqlite3_finalize(q);

  return info;
}

std::shared_ptr<agt::Session> SessionManager::open(const std::string& uuid) {
  auto path = sessions_dir_ / (uuid + ".db");
  return agt::make_sqlite_session(path.string(), "default");
}

std::vector<SessionInfo> SessionManager::list() const {
  std::vector<SessionInfo> result;

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_,
                     "SELECT uuid, name, provider, model, thinking_effort, "
                     "created_at, updated_at FROM sessions ORDER BY updated_at DESC",
                     -1, &stmt, nullptr);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    SessionInfo info;
    info.uuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    info.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    info.provider = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    info.model = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    info.thinking_effort = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    info.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    info.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    result.push_back(std::move(info));
  }
  sqlite3_finalize(stmt);

  return result;
}

void SessionManager::remove(const std::string& uuid) {
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, "DELETE FROM sessions WHERE uuid = ?", -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  auto path = sessions_dir_ / (uuid + ".db");
  std::filesystem::remove(path);
}

void SessionManager::rename(const std::string& uuid, const std::string& name) {
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, "UPDATE sessions SET name = ? WHERE uuid = ?", -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, uuid.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void SessionManager::update_config(const std::string& uuid, const ChattySettings& cfg) {
  auto provider = std::string(agt::provider_to_string(cfg.provider));
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_,
                     "UPDATE sessions SET provider = ?, model = ?, thinking_effort = ?, "
                     "updated_at = strftime('%Y-%m-%dT%H:%M:%S','now') WHERE uuid = ?",
                     -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, provider.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, cfg.model.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, cfg.thinking_effort.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, uuid.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void SessionManager::touch(const std::string& uuid) {
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_,
                     "UPDATE sessions SET updated_at = strftime('%Y-%m-%dT%H:%M:%S','now') "
                     "WHERE uuid = ?",
                     -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}
