#pragma once

#include "config.hpp"
#include <agt/agent.hpp>
#include <agt/llm.hpp>
#include <agt/runner.hpp>
#include <agt/session.hpp>
#include <memory>
#include <promptty/promptty.hpp>
#include <string>
#include <unordered_map>
#include <vector>

class Chatty {

  using command = std::function<void(const std::vector<std::string> &)>;

public:

  Chatty();
  ~Chatty() = default;

  void run() noexcept;

private:
  void handle_message(const std::string &input);
  void handle_command(const std::string &input);
  void command_provider(const std::vector<std::string> &args);
  void command_model(const std::vector<std::string> &args);
  void command_env(const std::vector<std::string> &args);
  void command_thinking(const std::vector<std::string> &args);
  void command_help(const std::vector<std::string> &args);

  void reset_editor();
  std::string make_prompt() const;

  std::unordered_map<std::string, command> commands_;
  std::unordered_map<agt::Provider, agt::ProviderConfig> providers_;
  ChattyConfig config_;
  std::optional<ptty::LineEditor> editor_;
  std::shared_ptr<agt::Llm> llm_;
  agt::Runner runner_;
  agt::Agent agent_;
};
