#pragma once

#include <agt/llm.hpp>
#include <string>
#include <unordered_map>

struct ChattySettings {
  agt::Provider provider;
  std::string model;
  std::string thinking_effort;
};

ChattySettings
load_settings(const std::unordered_map<agt::Provider, agt::ProviderConfig>& providers);

void save_settings(const ChattySettings& settings);
