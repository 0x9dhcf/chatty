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

ChattyConfig load_config(const std::unordered_map<agt::Provider, agt::ProviderConfig>& providers);
void save_config(const ChattyConfig& cfg);
std::filesystem::path history_path();
