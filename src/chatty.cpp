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
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mdtty/mdtty.hpp>
#include <print>
#include <ranges>
#include <sstream>
#include <string>
#include <vector>

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
          {"/rename", [this](const std::vector<std::string>& s) { command_rename(s); }},
          {"/auto", [this](const std::vector<std::string>& s) { command_auto(s); }},
          {"/reload", [this](const std::vector<std::string>& s) { command_reload(s); }},
          {"/help", [this](const std::vector<std::string>& s) { command_help(s); }},
      },
      policies_{
          {"shell",
           [this](const agt::Json& args) -> bool {
             if (!auto_approve_) {
               static const std::vector<std::string> choices = {"yes", "no",
                                                                "yes, enable auto-approve"};
               auto r = editor_->choose(choices, args["command"].dump() + ": needs approval");
               if (!r || r->value == "no")
                 return false;
               if (r->value == "yes, enable auto-approve") {
                 auto_approve_ = true;
                 reset_editor();
               }
             }
             return true;
           }},
      } {
}

void Chatty::reload() {
  environnement_ = Environment();

  providers_ = agt::load_providers_from_env();
  if (providers_.empty())
    throw std::runtime_error("no provider key found");

  // On first load, read persisted defaults; on subsequent reloads keep the
  // live provider/model/thinking selection the user made this session.
  if (!llm_) {
    settings_ = load_settings(providers_);
    llm_ = std::make_shared<agt::Llm>(settings_.provider, settings_.model,
                                      providers_[settings_.provider].key);
  }

  load_briefs();
  build_instructions();

  auto session = agent_.session ? agent_.session : std::make_shared<agt::MemorySession>();
  std::vector<std::shared_ptr<agt::Tool>> tools = {
      std::make_shared<Shell>(), std::make_shared<Spawn>(), std::make_shared<FileRead>(),
      std::make_shared<FileWrite>(), std::make_shared<Ask>()};
  if (const char* k = std::getenv("TAVILY_API_KEY"); k != nullptr && k[0] != '\0') {
    tools.push_back(std::make_shared<WebSearch>());
    tools.push_back(std::make_shared<WebExtract>());
  }
  agent_ = {
      .name = "chatty",
      .description = "A general-purpose terminal agent",
      .instructions = instructions_,
      .tools = std::move(tools),
      .session = std::move(session),
  };

  reset_editor();
}

void Chatty::load_briefs() {
  briefs_.clear();
  auto mdfiles = std::filesystem::directory_iterator(briefs_dir()) |
                 std::views::filter([](const std::filesystem::directory_entry& entry) {
                   return entry.is_regular_file() && entry.path().extension() == ".md";
                 });
  for (auto f : mdfiles) {
    auto path = f.path();
    auto name = path.filename().stem().string();
    std::ifstream file(path, std::ios::binary);
    if (!file)
      throw std::runtime_error("Cannot open file: " + path.string());
    std::string content(std::filesystem::file_size(path), '\0');
    file.read(content.data(), static_cast<std::streamsize>(content.size()));
    briefs_[name] = std::move(content);
  }
}

void Chatty::build_instructions() {
  std::ostringstream p;
  p << "You are Chatty a helpful terminal assistant. Keep responses concise and\n"
    << "well-suited for terminal output.\n"
    << "\n## Your Environment\n"
    << "- OS: " << environnement_.os << "\n"
    << "- Shell: " << environnement_.shell << "\n"
    << "- Desktop: " << environnement_.desktop << "\n"
    << "- Session: " << environnement_.session << "\n"
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
    << "- Do not use emoji or pictographic Unicode characters in\n"
    << "  the middle of a sentence, a list or a table.\n"
    << "  Plain ASCII headings, bullets, and punctuation only.\n"
    << "  Use markdown for emphasis, not glyphs.\n";

  if (const char* k = std::getenv("TAVILY_API_KEY"); k != nullptr && k[0] != '\0') {
    p << "- The web_search and web_extract tools are paid external API calls.\n"
      << "  Use them with parsimony:\n"
      << "    * Do not search for facts you can already answer from training\n"
      << "      or local context.\n"
      << "    * Prefer web_search first; only use web_extract when (a) the\n"
      << "      user gave you a specific URL, or (b) the search snippets were\n"
      << "      clearly insufficient to answer.\n"
      << "    * Do not chain extract over many URLs at once. One URL at a\n"
      << "      time unless you have a specific need.\n"
      << "    * Default to extract_depth='basic'; only request 'advanced'\n"
      << "      when basic returned too little.\n";
  }

  if (!briefs_.empty()) {
    p << "\n\n ## Per Topic Recommendations\n";
    for (const auto& [key, value] : briefs_) {
      p << "### " << key << "\n" << value << "\n";
    }
  }

  instructions_ = p.str();
}

std::string Chatty::make_prompt() const {
  std::string p;
  if (!compact_prompt_) {
    p += std::format("\x1b[36m{}\x1b[0m", agt::provider_to_string(settings_.provider));
    p += std::format(" \x1b[2m\xc2\xb7\x1b[0m "); // dim ·
    p += std::format("\x1b[33m{}\x1b[0m", settings_.model);
    if (!settings_.thinking_effort.empty())
      p += std::format(" \x1b[2m\xc2\xb7\x1b[0m \x1b[32m{}\x1b[0m", settings_.thinking_effort);
    if (auto_approve_)
      p += std::format(" \x1b[2m\xc2\xb7\x1b[0m \x1b[1;31mauto\x1b[0m");
  } else if (auto_approve_) {
    // Compact mode still surfaces auto-approve since it has consequences.
    p += std::format("\x1b[1;31mauto\x1b[0m");
  }
  p += std::format(" \x1b[1;35m\xe2\x9d\xaf\x1b[0m "); // bold magenta ❯
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
  editor_->bind_key(ptty::bindable_key::ctrl_t, [this] {
    compact_prompt_ = !compact_prompt_;
    editor_->set_prompt(ptty::Prompt(make_prompt()));
  });
}

void Chatty::run() noexcept {
  try {
    reload();
  } catch (const std::exception& e) {
    std::cerr << "Warning: " << e.what() << "\n";
  }

  while (auto line = editor_->get_line()) {
    if (line->empty())
      continue;
    if (line->starts_with('/'))
      handle_command(*line);
    else
      handle_message(*line);
  }
  std::println("\xf0\x9f\x91\x8b bye!"); // 👋
}

void Chatty::handle_message(const std::string& input) {
  try {
    mdtty::Renderer md([](std::string_view s) {
      std::print(stdout, "{}", s);
      std::fflush(stdout);
    });

    bool seen_content = false;
#ifndef NDEBUG
    using clock = std::chrono::steady_clock;
    auto tool_t0 = clock::now();
    auto llm_t0 = clock::now();
    auto elapsed_ms = [](clock::time_point t0) {
      return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();
    };
#endif

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
#ifndef NDEBUG
        .on_llm_start = [&llm_t0](const agt::Llm&, const agt::Json&) { llm_t0 = clock::now(); },
        .on_llm_stop =
            [&llm_t0, &elapsed_ms](const agt::Llm&, const agt::Json&) {
              std::print(stderr, "\x1b[2;34m[llm] {} ms\x1b[0m\n", elapsed_ms(llm_t0));
              std::fflush(stderr);
            },
#endif
        .on_tool_start = [this, &seen_content
#ifndef NDEBUG
                          ,
                          &tool_t0
#endif
        ](const agt::Tool& t, const agt::Json& args) -> bool {
          // After a tool call the model starts a fresh text block; allow
          // its leading whitespace to be trimmed too.
          seen_content = false;
#ifndef NDEBUG
          std::print(stderr, "\x1b[2;36m[tool] {}\x1b[0m", t.name());
          std::fflush(stderr);
          tool_t0 = clock::now();
#endif
          auto it = policies_.find(t.name());
          if (it != policies_.end())
            return it->second(args);
          return true;
        },
#ifndef NDEBUG
        .on_tool_stop =
            [&tool_t0, &elapsed_ms](const agt::Tool&, const agt::Json&, const agt::Json&) {
              std::print(stderr, "\x1b[2;36m {} ms\x1b[0m\n", elapsed_ms(tool_t0));
              std::fflush(stderr);
            },
#endif
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

  ChattySettings defaults{provider, model, levels[thinking_choice->index]};
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

  // Restore the messages
  mdtty::Renderer md([](std::string_view s) {
    std::print(stdout, "{}", s);
    std::fflush(stdout);
  });

  for (auto m : agent_.session->messages()) {
    auto role = m.value("role", "");
    std::string_view icon;
    if (role == "user")
      icon = "\xf0\x9f\x91\xa4 "; // 👤
    else if (role == "assistant")
      icon = "\xf0\x9f\xa4\x96 "; // 🤖
    else
      continue;
    std::print("{}", icon);
    md.feed(m["content"].get<std::string>());
    md.flush();
  }
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

void Chatty::command_rename(const std::vector<std::string>& args) {
  if (!session_persisted_) {
    std::println("current session is not saved");
    return;
  }
  if (args.size() < 2) {
    std::println("usage: /rename <new name>");
    return;
  }
  std::string name = args[1];
  for (std::size_t i = 2; i < args.size(); ++i) {
    name += ' ';
    name += args[i];
  }
  try {
    session_mgr_.rename(current_session_uuid_, name);
    std::println("renamed to: {}", name);
  } catch (const std::exception& e) {
    std::println("rename failed: {}", e.what());
  }
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
      std::println("  {}{}", m.id,
                   (p == settings_.provider && m.id == settings_.model) ? " *" : "");
  }
}

void Chatty::command_reload(const std::vector<std::string>&) {
  try {
    reload();
    std::println("reloaded");
  } catch (const std::exception& e) {
    std::println("reload failed: {}", e.what());
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
  std::println("/rename <name>      — rename the current session");
  std::println("/auto [on|off]      — auto-approve shell tool calls (toggle)");
  std::println("/reload             — reload environment, providers, briefs, instructions");
  std::println("/help               — show this help");
  std::println("Ctrl-D              — quit");
}
