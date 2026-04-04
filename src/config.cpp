#include "config.hpp"
#include "agt/llm.hpp"
#include <fstream>
#include <vector>

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#else
#include <pwd.h> 
#include <unistd.h>
#endif

[[nodiscard]] static std::optional<std::filesystem::path> home_dir() noexcept {
#ifdef _WIN32
  PWSTR wide = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &wide))) {
    std::filesystem::path result(wide);
    CoTaskMemFree(wide);
    return result;
  }
  // Fallback: %USERPROFILE%
  if (const char* up = std::getenv("USERPROFILE"))
    return std::filesystem::path(up);

  return std::nullopt;

#else
  if (const struct passwd* pw = getpwuid(getuid()))
    return std::filesystem::path(pw->pw_dir);

  // Fallback: $HOME env var
  if (const char* home = std::getenv("HOME"))
    return std::filesystem::path(home);

  return std::nullopt;
#endif
}

[[nodiscard]] static std::filesystem::path canonical_config_path() {
  const auto home = home_dir();
  if (!home)
    throw std::runtime_error("Cannot determine home directory");
  return *home / ".chatty.cfg";
}

std::filesystem::path history_path() {
  const auto home = home_dir();
  if (!home)
    throw std::runtime_error("Cannot determine home directory");
  return *home / ".chatty_history";
}

[[nodiscard]] static std::pair<std::string, std::string>
parse_line(std::string_view line) noexcept {
  // Strip comments and blank lines — caller must pre-filter
  const auto eq = line.find('=');
  if (eq == std::string_view::npos)
    return {};

  auto trim = [](std::string_view s) -> std::string {
    const auto a = s.find_first_not_of(" \t\r");
    const auto b = s.find_last_not_of(" \t\r");
    return (a == std::string_view::npos) ? "" : std::string(s.substr(a, b - a + 1));
  };

  return {trim(line.substr(0, eq)), trim(line.substr(eq + 1))};
}

ChattyConfig load_config(const std::unordered_map<agt::Provider, agt::ProviderConfig>& providers) {

  auto path = canonical_config_path();

  // find the smallest entry in the map key list
  auto it = std::min_element(providers.begin(), providers.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });

  // default to first provider first model
  ChattyConfig cfg{it->first, it->second.models[0].id, "low"};

  if (!std::filesystem::exists(path)) {
    if (const auto parent = path.parent_path(); !parent.empty())
      std::filesystem::create_directories(parent);
    save_config(cfg);
  }

  std::ifstream f(path);
  if (!f)
    throw std::runtime_error("Cannot open config: " + path.string());

  for (std::string raw; std::getline(f, raw);) {
    std::string_view line = raw;

    if (line.empty() || line.starts_with('#'))
      continue;

    auto [key, val] = parse_line(line);
    if (key.empty())
      continue;

    if (key == "provider")
      cfg.provider = agt::provider_from_string(val);
    else if (key == "model")
      cfg.model = val;
    else if (key == "thinking_effort")
      cfg.thinking_effort = val;
    // unknown keys silently ignored — forward compatibility
  }

  // cfg.validate();
  return cfg;
}

void save_config(const ChattyConfig& cfg) {
  // cfg.validate();

  auto path = canonical_config_path();

  std::ofstream f(path);
  if (!f)
    throw std::runtime_error("Cannot open config for writing: " + path.string());

  f << "# chatty config\n"
    << "provider=" << agt::provider_to_string(cfg.provider) << '\n'
    << "model=" << cfg.model << '\n'
    << "thinking_effort=" << cfg.thinking_effort << '\n';

  if (!f)
    throw std::runtime_error("Write failed: " + path.string());
}
