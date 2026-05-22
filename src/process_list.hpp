#pragma once

#include "process_registry.hpp"
#include "tool_context.hpp"
#include <agt/tool.hpp>
#include <chrono>
#include <ctime>
#include <string>

class ProcessList : public agt::Tool {
  const char* name() const noexcept override { return "process_list"; }
  const char* description() const noexcept override {
    return "List all supervised background processes started in this chatty "
           "session, with their handle, command, started_at timestamp, and "
           "current state (alive or exited with code).";
  }

  agt::Json parameters() const override {
    return {{"type", "object"}, {"properties", agt::Json::object()}};
  }

  agt::Json execute(const agt::Json& input, void* context = nullptr) override {
    (void)input;
    auto* ctx = static_cast<ChattyToolContext*>(context);
    if (!ctx || !ctx->registry)
      return std::string("error: no process registry context");

    auto handles = ctx->registry->list();
    if (ctx->refresh_prompt) ctx->refresh_prompt();
    agt::Json arr = agt::Json::array();
    for (const auto& h : handles) {
      char isobuf[32]{};
      auto t = std::chrono::system_clock::to_time_t(h.started_at);
      std::strftime(isobuf, sizeof(isobuf), "%Y-%m-%dT%H:%M:%S",
                    std::localtime(&t));
      agt::Json item = {
          {"handle", h.handle},
          {"command", h.command},
          {"pid", h.pid},
          {"started_at", isobuf},
          {"alive", !h.exit_code.has_value()},
      };
      if (h.exit_code) item["exit_code"] = *h.exit_code;
      arr.push_back(item);
    }
    return arr;
  }
};
