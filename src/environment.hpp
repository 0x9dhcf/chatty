#pragma once

#include "exec.hpp"

#include <cstdlib>
#include <string>

struct Environment {
  std::string os;
  std::string shell;
  std::string desktop;
  std::string session;

  Environment() {
    auto from_env = [](const char* name) -> std::string {
      const char* v = std::getenv(name);
      return v ? v : "unknown";
    };

    os = exec(". /etc/os-release 2>/dev/null && echo \"$PRETTY_NAME\" || uname -sr");
    shell = from_env("SHELL");
    desktop = from_env("XDG_SESSION_DESKTOP");
    session = from_env("XDG_SESSION_TYPE");
  }
};
