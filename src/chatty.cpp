#include "chatty.hpp"
#include "env.hpp"
#include "prompt.hpp"
#include "tools.hpp"
#include <agt/llm.hpp>
#include <agt/runner.hpp>
#include <agt/session.hpp>
#include <algorithm>
#include <cstdlib>
#include <print>
#include <sstream>
#include <string>
#include <vector>

static std::string build_instructions() {
  auto e = Env();
  std::ostringstream p;

  p << "You are a helpful terminal assistant. Keep responses concise and\n"
    << "well-suited for terminal output.\n"
    << "\n## Your Environment\n"
    << "- OS: " << e.os << "\n"
    << "- Shell: " << e.shell << "\n"
    << "- Desktop: " << e.desktop << "\n"
    << "- Session: " << e.session << "\n"
    << "\n## Rules\n"
    << "- NEVER fabricate paths, usernames, or system details. If you don't\n"
    << "  know something, use the shell tool to find out BEFORE acting.\n"
    << "  For example: echo $HOME, whoami, command -v, etc.\n"
    << "- If a tool fails or returns nothing, investigate further. Don't\n"
    << "  give up after one attempt. Think about what else could explain\n"
    << "  the result and try a different approach.\n";

  return p.str();
}

Chatty::Chatty()
    : commands_{
          {"/provider", [this](const std::vector<std::string>& s) { command_provider(s); }},
          {"/model", [this](const std::vector<std::string>& s) { command_model(s); }},
          {"/environment", [this](const std::vector<std::string>& s) { command_env(s); }},
          {"/thinking", [this](const std::vector<std::string>& s) { command_thinking(s); }},
          {"/help", [this](const std::vector<std::string>& s) { command_help(s); }},
      } {


  providers_ = agt::load_providers_from_env();
  if (providers_.empty())
    throw std::runtime_error("no provider key found");

  config_ = load_config(providers_);
  llm_ = std::make_shared<agt::Llm>(config_.provider, config_.model, providers_[config_.provider].key);

  auto shell_tool = std::make_shared<Shell>();
  auto spawn_tool = std::make_shared<Spawn>();
  auto read_tool = std::make_shared<FileRead>();
  auto write_tool = std::make_shared<FileWrite>();

  // agent
  agent_ = {
      .name = "chatty",
      .description = "A general-purpose terminal agent",
      .instructions = build_instructions(),
      .tools = {shell_tool, spawn_tool, read_tool, write_tool},
      .session = std::make_shared<agt::MemorySession>(),
  };
}

void Chatty::run() noexcept {
  while (auto line = prompt::input(std::format("[{} - {}{}] ",
             agt::provider_to_string(config_.provider), config_.model,
             config_.thinking_effort.empty() ? "" : " - " + config_.thinking_effort))) {
    if (line->empty())
      continue;
    if (line->starts_with('/'))
      handle_command(*line);
    else
      handle_message(*line);
  }
}

void Chatty::handle_message(const std::string& input) {
  try {
    agt::RunnerOptions opts = {.max_turns = 10, .thinking_effort = config_.thinking_effort};
    auto r = runner_.run(*llm_, agent_, input, opts);
    std::println(stdout, "{}", r.content);
  } catch (const agt::LlmError& e) {
    std::println(stderr, "{}", e.what());
  }
}

void Chatty::handle_command(const std::string& input) {
  std::vector<std::string> args;
  std::istringstream iss(input);
  std::string word;
  while (iss >> word)
    args.push_back(word);

  if (commands_.contains(args[0])) {
    commands_[args[0]](args);
  } else {
    std::println("command {} not found.", args[0]);
  }
}

void Chatty::command_provider(const std::vector<std::string>& args) {
  if (args.size() < 2) {
    // Build list of available providers
    std::vector<std::string_view> names;
    std::vector<agt::Provider> providers;
    for (auto& [p, cfg] : providers_) {
      names.push_back(agt::provider_to_string(p));
      providers.push_back(p);
    }

    auto choice = prompt::choose("Select provider:", names);
    if (!choice)
      return;

    auto p = providers[*choice];
    if (p != config_.provider) {
      config_.provider = p;
      config_.model = providers_[p].models[0].id;
      *llm_ = agt::Llm(config_.provider, config_.model, providers_[p].key);
      save_config(config_);
    }
    return;
  }

  try {
    auto p = agt::provider_from_string(args[1]);
    if (p != config_.provider) {
      config_.provider = p;
      config_.model = providers_[p].models[0].id;
      *llm_ = agt::Llm(config_.provider, config_.model, providers_[p].key);
      save_config(config_);
    }
  } catch (const std::exception& e) {
    std::println("{}", e.what());
  }
}

void Chatty::command_model(const std::vector<std::string>& args) {
  auto& models = providers_[config_.provider].models;

  if (args.size() < 2) {
    std::vector<std::string_view> names;
    for (auto& m : models)
      names.push_back(m.id);
    auto choice = prompt::choose("Select model:", names);
    if (!choice)
      return;
    config_.model = models[*choice].id;
  } else {
    auto it = std::ranges::find(models, args[1], &agt::ModelInfo::id);
    if (it == models.end()) {
      std::println("model '{}' not available", args[1]);
      return;
    }
    config_.model = it->id;
  }
  *llm_ = agt::Llm(config_.provider, config_.model, providers_[config_.provider].key);
  save_config(config_);
}

void Chatty::command_thinking(const std::vector<std::string>& args) {
  static const std::vector<std::string_view> levels = {"none", "low", "medium", "high"};

  if (args.size() < 2) {
    auto choice = prompt::choose("Thinking effort:", levels);
    if (!choice)
      return;
    config_.thinking_effort = levels[*choice];
  } else {
    if (std::ranges::find(levels, args[1]) == levels.end()) {
      std::println("invalid effort '{}' (none|low|medium|high)", args[1]);
      return;
    }
    config_.thinking_effort = args[1];
  }
  save_config(config_);
  std::println("thinking effort: {}", config_.thinking_effort);
}

void Chatty::command_env(const std::vector<std::string>&) {
  for (auto& [p, cfg] : providers_) {
    std::println("{}:", agt::provider_to_string(p));
    for (auto& m : cfg.models)
      std::println("  {}{}", m.id, (p == config_.provider && m.id == config_.model) ? " *" : "");
  }
}

void Chatty::command_help(const std::vector<std::string>&) {
  std::println("/provider [name]    — switch LLM provider");
  std::println("/model [name]       — switch model");
  std::println("/thinking [level]   — set thinking effort (none|low|medium|high)");
  std::println("/environment        — show available providers and models");
  std::println("/help               — show this help");
  std::println("Ctrl-D            — quit");
}
