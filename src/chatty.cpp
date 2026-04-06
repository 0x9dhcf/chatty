#include "chatty.hpp"
#include "env.hpp"
#include "tools.hpp"
#include <agt/llm.hpp>
#include <agt/runner.hpp>
#include <agt/session.hpp>
#include <mdtty/mdtty.hpp>
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
    << "  the result and try a different approach.\n"
    << "- When there are multiple valid options and the user hasn't specified\n"
    << "  a preference, ALWAYS use the ask tool to let them choose instead\n"
    << "  of picking for them or listing options in text.\n";

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
  reset_editor();

  auto shell_tool = std::make_shared<Shell>();
  auto spawn_tool = std::make_shared<Spawn>();
  auto read_tool = std::make_shared<FileRead>();
  auto write_tool = std::make_shared<FileWrite>();
  auto ask_tool = std::make_shared<Ask>();

  // agent
  agent_ = {
      .name = "chatty",
      .description = "A general-purpose terminal agent",
      .instructions = build_instructions(),
      .tools = {shell_tool, spawn_tool, read_tool, write_tool, ask_tool},
      .session = std::make_shared<agt::MemorySession>(),
  };
}

std::string Chatty::make_prompt() const {
  // provider \x1b[2m·\x1b[0m model [\x1b[2m·\x1b[0m thinking] \x1b[1m❯\x1b[0m
  std::string p;
  p += std::format("\x1b[36m{}\x1b[0m", agt::provider_to_string(config_.provider));
  p += std::format(" \x1b[2m\xc2\xb7\x1b[0m ");  // dim ·
  p += std::format("\x1b[33m{}\x1b[0m", config_.model);
  if (!config_.thinking_effort.empty())
    p += std::format(" \x1b[2m\xc2\xb7\x1b[0m \x1b[32m{}\x1b[0m", config_.thinking_effort);
  p += std::format(" \x1b[1;35m\xe2\x9d\xaf\x1b[0m ");  // bold magenta ❯
  return p;
}

void Chatty::reset_editor() {
  editor_.emplace(ptty::Prompt(make_prompt()), history_path());
  editor_->set_completion([this](ptty::CompletionRequest req) -> ptty::CompletionResult {
    auto start = req.cursor;
    while (start > 0 && req.buffer[start - 1] != ' ' && req.buffer[start - 1] != '\n')
      --start;
    auto prefix = req.buffer.substr(start, req.cursor - start);
    if (!prefix.starts_with('/'))
      return {start, req.cursor - start, {}};
    std::vector<std::string> matches;
    for (auto& [cmd, _] : commands_)
      if (cmd.starts_with(prefix))
        matches.push_back(cmd);
    std::ranges::sort(matches);
    return {start, req.cursor - start, std::move(matches)};
  });
}

void Chatty::run() noexcept {
  while (auto line = editor_->get_line()) {
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
    mdtty::Renderer md(
        [](std::string_view s) { std::print(stdout, "{}", s); std::fflush(stdout); });

    agt::RunnerOptions opts = {.max_turns = 10, .context = &*editor_,
                               .thinking_effort = config_.thinking_effort};
    agt::RunnerHooks hooks = {.on_token = [&md](const std::string& tok) { md.feed(tok); }};

    runner_.run(*llm_, agent_, input, opts, hooks);
    md.flush();
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
    std::vector<std::string> names;
    std::vector<agt::Provider> providers;
    for (auto& [p, cfg] : providers_) {
      names.push_back(std::string(agt::provider_to_string(p)));
      providers.push_back(p);
    }

    auto choice = editor_->choose(names);
    if (!choice)
      return;

    auto p = providers[choice->index];
    if (p != config_.provider) {
      config_.provider = p;
      config_.model = providers_[p].models[0].id;
      *llm_ = agt::Llm(config_.provider, config_.model, providers_[p].key);
      save_config(config_);
      reset_editor();
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
      reset_editor();
    }
  } catch (const std::exception& e) {
    std::println("{}", e.what());
  }
}

void Chatty::command_model(const std::vector<std::string>& args) {
  auto& models = providers_[config_.provider].models;

  if (args.size() < 2) {
    std::vector<std::string> names;
    for (auto& m : models)
      names.push_back(m.id);
    auto choice = editor_->choose(names);
    if (!choice)
      return;
    config_.model = models[choice->index].id;
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
  reset_editor();
}

void Chatty::command_thinking(const std::vector<std::string>& args) {
  static const std::vector<std::string> levels = {"low", "medium", "high"};

  if (args.size() < 2) {
    auto choice = editor_->choose(levels);
    if (!choice)
      return;
    config_.thinking_effort = levels[choice->index];
  } else {
    if (std::ranges::find(levels, args[1]) == levels.end()) {
      std::println("invalid effort '{}' (none|low|medium|high)", args[1]);
      return;
    }
    config_.thinking_effort = args[1];
  }
  save_config(config_);
  reset_editor();
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
  std::println("Ctrl-D              — quit");
}
