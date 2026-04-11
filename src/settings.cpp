#include "settings.hpp"

#include "paths.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <toml++/toml.hpp>

static ChattySettings
default_settings(const std::unordered_map<agt::Provider, agt::ProviderConfig>& providers) {
  auto it = std::min_element(providers.begin(), providers.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
  return {it->first, it->second.models[0].id, "low", {}};
}

ChattySettings
load_settings(const std::unordered_map<agt::Provider, agt::ProviderConfig>& providers) {
  const auto path = setting_path();
  if (!std::filesystem::exists(path)) {
    auto s = default_settings(providers);
    save_settings(s);
    return s;
  }

  toml::table tbl;
  try {
    tbl = toml::parse_file(path.string());
  } catch (const toml::parse_error& e) {
    throw std::runtime_error("failed to parse settings file: " + std::string(e.what()));
  }

  ChattySettings s{};

  const auto provider = tbl["provider"].value<std::string>();
  if (!provider)
    throw std::runtime_error("settings: missing 'provider'");
  s.provider = agt::provider_from_string(*provider);

  const auto model = tbl["model"].value<std::string>();
  if (!model)
    throw std::runtime_error("settings: missing 'model'");
  s.model = *model;

  s.thinking_effort = tbl["thinking_effort"].value_or<std::string>("low");

  if (const auto* arr = tbl["enabled_briefs"].as_array()) {
    s.enabled_briefs.reserve(arr->size());
    for (const auto& node : *arr) {
      if (const auto v = node.value<std::string>())
        s.enabled_briefs.push_back(*v);
    }
  }

  return s;
}

void save_settings(const ChattySettings& settings) {
  toml::array briefs;
  for (const auto& b : settings.enabled_briefs)
    briefs.push_back(b);

  toml::table tbl{
      {"provider", std::string(agt::provider_to_string(settings.provider))},
      {"model", settings.model},
      {"thinking_effort", settings.thinking_effort},
      {"enabled_briefs", std::move(briefs)},
  };

  const auto path = setting_path();
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out)
    throw std::runtime_error("failed to open settings file for writing: " + path.string());
  out << tbl;
}
