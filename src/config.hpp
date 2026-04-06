#pragma once

#include "agt/llm.hpp"
#include <filesystem>
#include <string>
#include <unordered_map>

struct ChattyConfig {
  agt::Provider provider;
  std::string model;
  std::string thinking_effort;
};

ChattyConfig default_config(const std::unordered_map<agt::Provider, agt::ProviderConfig>& providers);
std::filesystem::path config_dir();
std::filesystem::path data_dir();
std::filesystem::path history_path();
