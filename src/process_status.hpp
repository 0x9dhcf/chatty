#pragma once

#include "process_registry.hpp"
#include "tool_context.hpp"
#include <agt/tool.hpp>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

class ProcessStatus : public agt::Tool {
  const char* name() const noexcept override { return "process_status"; }
  const char* description() const noexcept override {
    return "Get the status of a supervised background process: whether it's "
           "still running, its exit code if finished, and the tail of its "
           "captured output. Pair with spawn_supervised.";
  }

  agt::Json parameters() const override {
    return {{"type", "object"},
            {"properties",
             {{"handle", {{"type", "string"},
                          {"description", "handle returned by spawn_supervised"}}},
              {"tail_lines", {{"type", "integer"},
                              {"description", "number of trailing log lines to return (default 50)"}}}}},
            {"required", {"handle"}}};
  }

  agt::Json execute(const agt::Json& input, void* context = nullptr) override {
    auto* ctx = static_cast<ChattyToolContext*>(context);
    if (!ctx || !ctx->registry)
      return std::string("error: no process registry context");

    auto h = input["handle"].get<std::string>();
    int tail_lines = input.value("tail_lines", 50);

    ctx->registry->reap_all();
    if (ctx->refresh_prompt) ctx->refresh_prompt();
    auto info = ctx->registry->get(h);
    if (!info)
      return "error: no such handle: " + h;

    ProcessRegistry::truncate_log_if_needed(info->log_path);

    std::string content;
    {
      std::ifstream in(info->log_path, std::ios::binary);
      if (in) {
        std::ostringstream buf;
        buf << in.rdbuf();
        content = buf.str();
      }
    }
    std::string tail = last_n_lines(content, tail_lines);

    char isobuf[32]{};
    auto t = std::chrono::system_clock::to_time_t(info->started_at);
    std::strftime(isobuf, sizeof(isobuf), "%Y-%m-%dT%H:%M:%S",
                  std::localtime(&t));

    agt::Json result = {
        {"handle", info->handle},
        {"command", info->command},
        {"pid", info->pid},
        {"started_at", isobuf},
        {"alive", !info->exit_code.has_value()},
        {"output_tail", tail},
        {"log_path", info->log_path.string()},
    };
    if (info->exit_code) result["exit_code"] = *info->exit_code;
    return result;
  }

private:
  static std::string last_n_lines(const std::string& s, int n) {
    if (n <= 0 || s.empty()) return {};
    std::vector<std::size_t> newlines;
    for (std::size_t i = 0; i < s.size(); ++i)
      if (s[i] == '\n') newlines.push_back(i);
    std::size_t total = newlines.size() + (s.back() != '\n' ? 1 : 0);
    if (static_cast<std::size_t>(n) >= total) return s;
    std::size_t skip_idx = total - static_cast<std::size_t>(n) - 1;
    return s.substr(newlines[skip_idx] + 1);
  }
};
