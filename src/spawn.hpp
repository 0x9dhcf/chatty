#pragma once

#include <agt/tool.hpp>
#include <fcntl.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

class Spawn : public agt::Tool {
  const char* name() const noexcept override { return "spawn"; }
  const char* description() const noexcept override {
    return "Launch a process in the background (fire and forget). "
           "Use this for GUI applications, editors, browsers, and any long-running process.";
  }

  agt::Json parameters() const override {
    return {{"type", "object"},
            {"properties",
             {{"command", {{"type", "string"}, {"description", "the command to spawn"}}}}},
            {"required", {"command"}}};
  }

  agt::Json execute(const agt::Json& input, void* context = nullptr) override {
    (void)context;
    auto cmd = input["command"].get<std::string>();
    pid_t pid = fork();
    if (pid < 0)
      return "error: fork() failed";

    if (pid == 0) {
      // Double-fork: intermediate child exits immediately so the
      // grandchild is reparented to init — no zombie.
      pid_t pid2 = fork();
      if (pid2 > 0)
        _exit(0);
      if (pid2 < 0)
        _exit(1);
      setsid();
      int devnull = open("/dev/null", O_RDWR);
      if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO)
          close(devnull);
      }
      execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
      _exit(127);
    }

    // Reap the intermediate child immediately.
    waitpid(pid, nullptr, 0);
    return "spawned";
  }
};
