#pragma once

#include "agt/tool.hpp"
#include "exec.hpp"
#include <cstdio>
#include <exception>
#include <string>

class Shell : public agt::Tool {
  const char *name() const noexcept override { return "shell"; }
  const char *description() const noexcept override {
    return "Execute a command in a shell and return the output";
  }

  agt::Json parameters() const override {
    return {{"type", "object"},
            {"properties",
             {{"command", {{"type", "string"}, {"description", "the command to execute"}}}}},
            {"required", {"command"}}};
  }

  agt::Json execute(const agt::Json &input, void *context = nullptr) override {
    (void)context;
    try {
      static constexpr size_t max_output = 8000;
      auto result = exec(input["command"].get<std::string>());
      if (result.size() > max_output) {
        auto total = result.size();
        result.resize(max_output);
        result += "\n... (output truncated, showing " + std::to_string(max_output) + " of " +
                  std::to_string(total) + " bytes)";
      }
      return result;
    } catch (const std::exception &e) {
      return e.what();
    }
  }
};
