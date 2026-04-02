#pragma once

#include <string>

struct Env {
  Env();
  std::string os;
  std::string shell;
  std::string desktop;
  std::string session;
};
