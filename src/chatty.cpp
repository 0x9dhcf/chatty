#include "chatty.hpp"
#include "environment.hpp"
#include "paths.hpp"
#include "tools.hpp"
#include <agt/json.hpp>
#include <agt/llm.hpp>
#include <agt/runner.hpp>
#include <agt/session.hpp>
#include <agt/tool.hpp>
#include <algorithm>
#include <cstdlib>
#include <mdtty/mdtty.hpp>
#include <print>
#include <sstream>
#include <string>
#include <vector>

static std::string build_instructions() {
  auto e = Environment();
  std::ostringstream p;

  p << "You are Chatty a helpful terminal assistant. Keep responses concise and\n"
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
    << "- For genuine tool errors (timeouts, missing files, command-not-\n"
    << "  found, malformed output), investigate further. Don't give up\n"
    << "  after one attempt. Think about what else could explain the\n"
    << "  result and try a different approach.\n"
    << "- If a tool result is exactly {\"error\": \"tool call denied\"}, the\n"
    << "  user explicitly refused that action. This is NOT a normal error\n"
    << "  to retry or work around. Do not propose the same call with\n"
    << "  different arguments, do not try a near-equivalent tool, do not\n"
    << "  loop. Either continue helpfully without that capability, or\n"
    << "  stop and tell the user plainly what you would have needed and\n"
    << "  why.\n"
    << "- When a request is ambiguous and there are several reasonable ways\n"
    << "  to fulfill it, use the ask tool to let the user choose instead\n"
    << "  of picking for them. Do not use it for trivial requests.\n"
    << "- Do not use emoji or pictographic Unicode characters in your\n"
    << "  responses. Plain ASCII headings, bullets, and punctuation only.\n"
    << "  Use markdown for emphasis, not glyphs.\n";

  return p.str();
}

Chatty::Chatty()
    : commands_{
          {"/provider", [this](const std::vector<std::string>& s) { command_provider(s); }},
          {"/model", [this](const std::vector<std::string>& s) { command_model(s); }},
          {"/environment", [this](const std::vector<std::string>& s) { command_env(s); }},
          {"/thinking", [this](const std::vector<std::string>& s) { command_thinking(s); }},
          {"/default", [this](const std::vector<std::string>& s) { command_default(s); }},
          {"/new", [this](const std::vector<std::string>& s) { command_new(s); }},
          {"/resume", [this](const std::vector<std::string>& s) { command_resume(s); }},
          {"/delete", [this](const std::vector<std::string>& s) { command_delete(s); }},
          {"/auto", [this](const std::vector<std::string>& s) { command_auto(s); }},
          {"/help", [this](const std::vector<std::string>& s) { command_help(s); }},
      } {

  providers_ = agt::load_providers_from_env();
  if (providers_.empty())
    throw std::runtime_error("no provider key found");

  settings_ = load_settings(providers_);
  llm_ =
      std::make_shared<agt::Llm>(settings_.provider, settings_.model, providers_[settings_.provider].key);
  reset_editor();

  auto shell_tool = std::make_shared<Shell>();
  auto spawn_tool = std::make_shared<Spawn>();
  auto read_tool = std::make_shared<FileRead>();
  auto write_tool = std::make_shared<FileWrite>();
  auto ask_tool = std::make_shared<Ask>();

  agent_ = {
      .name = "chatty",
      .description = "A general-purpose terminal agent",
      .instructions = build_instructions(),
      .tools = {shell_tool, spawn_tool, read_tool, write_tool, ask_tool},
      .session = std::make_shared<agt::MemorySession>(),
  };

  policies_["shell"] = [this](const agt::Json& args) -> bool {
    if (!auto_approve_) {
      static const std::vector<std::string> choices = {"yes", "no", "yes, enable auto-approve"};
      auto r = editor_->choose(choices, args["command"].dump() + " needs approval:");
      if (!r || r->value == "no")
        return false;
      if (r->value == "yes, enable auto-approve") {
        auto_approve_ = true;
        reset_editor();
      }
    }
    return true;
  };
}

std::string Chatty::make_prompt() const {
  // provider \x1b[2m·\x1b[0m model [\x1b[2m·\x1b[0m thinking] \x1b[1m❯\x1b[0m
  std::string p;
  p += std::format("\x1b[36m{}\x1b[0m", agt::provider_to_string(settings_.provider));
  p += std::format(" \x1b[2m\xc2\xb7\x1b[0m ");  // dim ·
  p += std::format("\x1b[33m{}\x1b[0m", settings_.model);
  if (!settings_.thinking_effort.empty())
    p += std::format(" \x1b[2m\xc2\xb7\x1b[0m \x1b[32m{}\x1b[0m", settings_.thinking_effort);
  if (auto_approve_)
    p += std::format(" \x1b[2m\xc2\xb7\x1b[0m \x1b[1;31mauto\x1b[0m");
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
    mdtty::Renderer md([](std::string_view s) {
      std::print(stdout, "{}", s);
      std::fflush(stdout);
    });

    bool seen_content = false;

    agt::RunnerOptions opts = {
        .max_turns = 10, .context = &*editor_, .thinking_effort = settings_.thinking_effort};
    agt::RunnerHooks hooks = {
        .on_token =
            [&md, &seen_content](const std::string& tok) {
              if (!seen_content) {
                // Skip leading whitespace the model often emits before its
                // first real character.
                auto first = tok.find_first_not_of(" \t\r\n");
                if (first == std::string::npos)
                  return;
                seen_content = true;
                md.feed(std::string_view(tok).substr(first));
                return;
              }
              md.feed(tok);
            },
        .on_tool_start =
            [this, &seen_content](const agt::Tool& t, const agt::Json& args) -> bool {
              // After a tool call the model starts a fresh text block; allow
              // its leading whitespace to be trimmed too.
              seen_content = false;
              auto it = policies_.find(t.name());
              if (it != policies_.end())
                return it->second(args);
              return true;
            },
    };

    runner_.run(*llm_, agent_, input, opts, hooks);
    md.flush();

    if (!session_persisted_) {
      // Truncate input to ~80 chars at word boundary for the session name
      std::string name = input;
      if (name.size() > 80) {
        name.resize(80);
        auto pos = name.find_last_of(' ');
        if (pos != std::string::npos)
          name.resize(pos);
      }

      auto info = session_mgr_.create(name, settings_);
      current_session_uuid_ = info.uuid;

      // Copy messages from memory session to persistent session
      auto sqlite_session = session_mgr_.open(info.uuid);
      auto msgs = agent_.session->messages();
      if (!msgs.empty())
        sqlite_session->append(msgs);

      agent_.session = sqlite_session;
      session_persisted_ = true;
    } else {
      session_mgr_.touch(current_session_uuid_);
    }
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
    if (p != settings_.provider) {
      settings_.provider = p;
      settings_.model = providers_[p].models[0].id;
      *llm_ = agt::Llm(settings_.provider, settings_.model, providers_[p].key);
      save_session_config();
      reset_editor();
    }
    return;
  }

  try {
    auto p = agt::provider_from_string(args[1]);
    if (p != settings_.provider) {
      settings_.provider = p;
      settings_.model = providers_[p].models[0].id;
      *llm_ = agt::Llm(settings_.provider, settings_.model, providers_[p].key);
      save_session_config();
      reset_editor();
    }
  } catch (const std::exception& e) {
    std::println("{}", e.what());
  }
}

void Chatty::command_model(const std::vector<std::string>& args) {
  auto& models = providers_[settings_.provider].models;

  if (args.size() < 2) {
    std::vector<std::string> names;
    for (auto& m : models)
      names.push_back(m.id);
    auto choice = editor_->choose(names);
    if (!choice)
      return;
    settings_.model = models[choice->index].id;
  } else {
    auto it = std::ranges::find(models, args[1], &agt::ModelInfo::id);
    if (it == models.end()) {
      std::println("model '{}' not available", args[1]);
      return;
    }
    settings_.model = it->id;
  }
  *llm_ = agt::Llm(settings_.provider, settings_.model, providers_[settings_.provider].key);
  save_session_config();
  reset_editor();
}

void Chatty::command_thinking(const std::vector<std::string>& args) {
  static const std::vector<std::string> levels = {"low", "medium", "high"};

  if (args.size() < 2) {
    auto choice = editor_->choose(levels);
    if (!choice)
      return;
    settings_.thinking_effort = levels[choice->index];
  } else {
    if (std::ranges::find(levels, args[1]) == levels.end()) {
      std::println("invalid effort '{}' (none|low|medium|high)", args[1]);
      return;
    }
    settings_.thinking_effort = args[1];
  }
  save_session_config();
  reset_editor();
}

void Chatty::command_default(const std::vector<std::string>&) {
  std::vector<std::string> provider_names;
  std::vector<agt::Provider> providers;
  for (auto& [p, cfg] : providers_) {
    provider_names.push_back(std::string(agt::provider_to_string(p)));
    providers.push_back(p);
  }
  auto provider_choice = editor_->choose(provider_names, "default provider:");
  if (!provider_choice)
    return;
  auto provider = providers[provider_choice->index];

  auto& models = providers_[provider].models;
  std::vector<std::string> model_names;
  for (auto& m : models)
    model_names.push_back(m.id);
  auto model_choice = editor_->choose(model_names, "default model:");
  if (!model_choice)
    return;
  auto model = models[model_choice->index].id;

  static const std::vector<std::string> levels = {"low", "medium", "high"};
  auto thinking_choice = editor_->choose(levels, "default thinking effort:");
  if (!thinking_choice)
    return;

  ChattySettings defaults{provider, model, levels[thinking_choice->index], settings_.enabled_briefs};
  try {
    save_settings(defaults);
    std::println("defaults saved: {} / {} / {}", agt::provider_to_string(defaults.provider),
                 defaults.model, defaults.thinking_effort);
  } catch (const std::exception& e) {
    std::println("failed to save defaults: {}", e.what());
  }
}

void Chatty::save_session_config() {
  if (session_persisted_)
    session_mgr_.update_config(current_session_uuid_, settings_);
}

void Chatty::start_new_session() {
  agent_.session = std::make_shared<agt::MemorySession>();
  current_session_uuid_.clear();
  session_persisted_ = false;
}

void Chatty::command_new(const std::vector<std::string>&) {
  start_new_session();
  std::println("new session started");
}

void Chatty::command_resume(const std::vector<std::string>&) {
  auto sessions = session_mgr_.list();
  if (sessions.empty()) {
    std::println("no saved sessions");
    return;
  }

  std::vector<std::string> names;
  for (auto& s : sessions)
    names.push_back(std::format("{} ({})", s.name, s.updated_at));

  auto choice = editor_->choose(names);
  if (!choice)
    return;

  auto& info = sessions[choice->index];
  agent_.session = session_mgr_.open(info.uuid);
  current_session_uuid_ = info.uuid;
  session_persisted_ = true;

  // Restore per-session config
  if (!info.provider.empty() && providers_.contains(agt::provider_from_string(info.provider))) {
    settings_.provider = agt::provider_from_string(info.provider);
    settings_.model = info.model;
    settings_.thinking_effort = info.thinking_effort;
    *llm_ = agt::Llm(settings_.provider, settings_.model, providers_[settings_.provider].key);
    reset_editor();
  }

  std::println("resumed: {}", info.name);
}

void Chatty::command_auto(const std::vector<std::string>& args) {
  if (args.size() < 2) {
    auto_approve_ = !auto_approve_;
  } else if (args[1] == "on") {
    auto_approve_ = true;
  } else if (args[1] == "off") {
    auto_approve_ = false;
  } else {
    std::println("usage: /auto [on|off]");
    return;
  }
  // std::println("auto-approve: {}", auto_approve_ ? "on" : "off");
  reset_editor();
}

void Chatty::command_delete(const std::vector<std::string>&) {
  if (!session_persisted_) {
    std::println("current session is not saved");
    return;
  }
  session_mgr_.remove(current_session_uuid_);
  start_new_session();
  std::println("session deleted, new session started");
}

void Chatty::command_env(const std::vector<std::string>&) {
  for (auto& [p, cfg] : providers_) {
    std::println("{}:", agt::provider_to_string(p));
    for (auto& m : cfg.models)
      std::println("  {}{}", m.id, (p == settings_.provider && m.id == settings_.model) ? " *" : "");
  }
}

void Chatty::command_help(const std::vector<std::string>&) {
  std::println("/provider [name]    — switch LLM provider");
  std::println("/model [name]       — switch model");
  std::println("/thinking [level]   — set thinking effort (none|low|medium|high)");
  std::println("/default            — save current provider/model/thinking as defaults");
  std::println("/environment        — show available providers and models");
  std::println("/new                — start a new session");
  std::println("/resume             — resume a saved session");
  std::println("/delete             — delete current session and start new");
  std::println("/auto [on|off]      — auto-approve shell tool calls (toggle)");
  std::println("/help               — show this help");
  std::println("Ctrl-D              — quit");
}
