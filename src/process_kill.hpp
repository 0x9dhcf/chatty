#pragma once

#include "process_registry.hpp"
#include "tool_context.hpp"
#include <agt/tool.hpp>
#include <signal.h>
#include <string>

class ProcessKill : public agt::Tool {
  const char* name() const noexcept override { return "process_kill"; }
  const char* description() const noexcept override {
    return "Send a signal to a supervised background process. Default signal "
           "is TERM (graceful). Pass KILL for an unconditional terminate, HUP "
           "or INT for variants. Pair with spawn_supervised + process_status.";
  }

  agt::Json parameters() const override {
    return {{"type", "object"},
            {"properties",
             {{"handle", {{"type", "string"},
                          {"description", "handle returned by spawn_supervised"}}},
              {"signal", {{"type", "string"},
                          {"enum", {"TERM", "KILL", "HUP", "INT"}},
                          {"description", "signal name (default TERM)"}}}}},
            {"required", {"handle"}}};
  }

  agt::Json execute(const agt::Json& input, void* context = nullptr) override {
    auto* ctx = static_cast<ChattyToolContext*>(context);
    if (!ctx || !ctx->registry)
      return std::string("error: no process registry context");

    auto h = input["handle"].get<std::string>();
    auto sig_name = input.value("signal", std::string("TERM"));

    int sig = 0;
    if (sig_name == "TERM") sig = SIGTERM;
    else if (sig_name == "KILL") sig = SIGKILL;
    else if (sig_name == "HUP") sig = SIGHUP;
    else if (sig_name == "INT") sig = SIGINT;
    else
      return "error: unsupported signal '" + sig_name +
             "' (use TERM, KILL, HUP, or INT)";

    if (!ctx->registry->kill(h, sig))
      return "error: no such handle, or process already exited: " + h;
    if (ctx->refresh_prompt) ctx->refresh_prompt();
    return "ok";
  }
};
