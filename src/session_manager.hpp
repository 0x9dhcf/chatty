#pragma once

#include "settings.hpp"
#include <agt/session.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct sqlite3;

struct SessionInfo {
  std::string uuid;
  std::string name;
  std::string provider;
  std::string model;
  std::string thinking_effort;
  std::string created_at;
  std::string updated_at;
};

class SessionManager {
public:
  SessionManager();
  ~SessionManager();

  SessionManager(const SessionManager&) = delete;
  SessionManager& operator=(const SessionManager&) = delete;
  SessionManager(SessionManager&&) = delete;
  SessionManager& operator=(SessionManager&&) = delete;

  SessionInfo create(const std::string& name, const ChattySettings& cfg);
  std::shared_ptr<agt::Session> open(const std::string& uuid);
  std::vector<SessionInfo> list() const;
  void remove(const std::string& uuid);
  void rename(const std::string& uuid, const std::string& name);
  void update_config(const std::string& uuid, const ChattySettings& cfg);
  void touch(const std::string& uuid);

private:
  std::filesystem::path sessions_dir_;
  sqlite3* db_ = nullptr;
};
