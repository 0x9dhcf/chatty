#pragma once

#include "process_registry.hpp"
#include "tool_context.hpp"
#include <agt/tool.hpp>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

class SpawnSupervised : public agt::Tool {
  const char* name() const noexcept override { return "spawn_supervised"; }
  const char* description() const noexcept override {
    return "Launch a background process AND capture its stdout + stderr to a "
           "log file. Returns a handle for later inspection via process_status, "
           "process_kill, or /ps. Use this for dev servers, builds, log "
           "followers — any process you'll want to check on, kill, or read "
           "output from later. For pure fire-and-forget GUI launches "
           "(editor, browser, terminal app) use the simpler `spawn` tool "
           "instead — it doesn't tie the process's lifetime to chatty.";
  }

  agt::Json parameters() const override {
    return {{"type", "object"},
            {"properties",
             {{"command", {{"type", "string"},
                           {"description", "shell command line to launch (executed via /bin/sh -c)"}}}}},
            {"required", {"command"}}};
  }

  agt::Json execute(const agt::Json& input, void* context = nullptr) override {
    auto* ctx = static_cast<ChattyToolContext*>(context);
    if (!ctx || !ctx->registry)
      return std::string("error: no process registry context");

    auto cmd = input["command"].get<std::string>();

    // Generate a unique log path under /tmp.
    auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();
    auto log_path = std::filesystem::path("/tmp") /
                    std::format("chatty-spawn-{}-{}.log", ::getpid(), now_ns);

    int log_fd = ::open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (log_fd < 0)
      return std::string("error: can't create log file ") + log_path.string() +
             ": " + std::strerror(errno);

    pid_t pid = fork();
    if (pid < 0) {
      ::close(log_fd);
      std::error_code ec;
      std::filesystem::remove(log_path, ec);
      return "error: fork() failed";
    }

    if (pid == 0) {
      ::setsid();
      int devnull = ::open("/dev/null", O_RDONLY);
      if (devnull >= 0) {
        ::dup2(devnull, STDIN_FILENO);
        if (devnull > STDERR_FILENO) ::close(devnull);
      }
      ::dup2(log_fd, STDOUT_FILENO);
      ::dup2(log_fd, STDERR_FILENO);
      if (log_fd > STDERR_FILENO) ::close(log_fd);
      ::execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
      ::_exit(127);
    }

    ::close(log_fd);
    auto handle = ctx->registry->register_started(pid, cmd, log_path);
    if (ctx->refresh_prompt) ctx->refresh_prompt();

    return agt::Json{
        {"handle", handle},
        {"pid", pid},
        {"log_path", log_path.string()},
    };
  }
};
