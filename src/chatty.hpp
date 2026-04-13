#pragma once

#include "environment.hpp"
#include "settings.hpp"
#include "session_manager.hpp"
#include <agt/agent.hpp>
#include <agt/json.hpp>
#include <agt/llm.hpp>
#include <agt/mcp.hpp>
#include <agt/runner.hpp>
#include <agt/session.hpp>
#include <functional>
#include <memory>
#include <promptty/promptty.hpp>
#include <string>
#include <unordered_map>
#include <vector>

class Chatty {

  using Command = std::function<void(const std::vector<std::string> &)>;
  using ToolPolicy = std::function<bool(const agt::Json &)>;

public:

  Chatty();
  ~Chatty() = default;

  void run() noexcept;

private:
  void reload();

  void handle_message(const std::string &input);
  void handle_command(const std::string &input);
  void command_provider(const std::vector<std::string> &args);
  void command_model(const std::vector<std::string> &args);
  void command_env(const std::vector<std::string> &args);
  void command_thinking(const std::vector<std::string> &args);
  void command_default(const std::vector<std::string> &args);
  void command_new(const std::vector<std::string> &args);
  void command_resume(const std::vector<std::string> &args);
  void command_delete(const std::vector<std::string> &args);
  void command_rename(const std::vector<std::string> &args);
  void command_auto(const std::vector<std::string> &args);
  void command_reload(const std::vector<std::string> &args);
  void command_mcp(const std::vector<std::string> &args);
  void command_help(const std::vector<std::string> &args);

  void start_new_session();
  void save_session_config();
  void load_briefs();
  void build_instructions();
  void reset_editor();
  std::string make_prompt() const;

  std::string instructions_;
  std::unordered_map<std::string, Command> commands_;
  std::unordered_map<agt::Provider, agt::ProviderConfig> providers_;
  std::unordered_map<std::string, ToolPolicy> policies_;
  std::unordered_map<std::string, std::string> briefs_;
  Environment environnement_;
  ChattySettings settings_;

  std::vector<std::unique_ptr<agt::McpServer>> mcp_servers_;
  std::unordered_map<std::string, std::string> mcp_tool_origin_;

  SessionManager session_mgr_;
  std::string current_session_uuid_;
  bool session_persisted_ = false;
  bool auto_approve_ = false;
  bool compact_prompt_ = false;
  std::optional<ptty::LineEditor> editor_;
  std::shared_ptr<agt::Llm> llm_;
  agt::Runner runner_;
  agt::Agent agent_;
};
