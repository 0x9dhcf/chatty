#include "config.hpp"

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
  if (const char* up = std::getenv("USERPROFILE"))
    return std::filesystem::path(up);
  return std::nullopt;
#else
  if (const struct passwd* pw = getpwuid(getuid()))
    return std::filesystem::path(pw->pw_dir);
  if (const char* home = std::getenv("HOME"))
    return std::filesystem::path(home);
  return std::nullopt;
#endif
}

std::filesystem::path config_dir() {
  const auto home = home_dir();
  if (!home)
    throw std::runtime_error("Cannot determine home directory");
#ifdef _WIN32
  return *home / "AppData" / "Roaming" / "chatty";
#else
  return *home / ".config" / "chatty";
#endif
}

std::filesystem::path data_dir() {
  const auto home = home_dir();
  if (!home)
    throw std::runtime_error("Cannot determine home directory");
#ifdef _WIN32
  return *home / "AppData" / "Local" / "chatty";
#else
  return *home / ".local" / "share" / "chatty";
#endif
}

std::filesystem::path history_path() {
  auto path = config_dir() / "history";

  // Migrate old history file
  if (!std::filesystem::exists(path)) {
    auto home = home_dir();
    if (home) {
      auto old_hist = *home / ".chatty_history";
      if (std::filesystem::exists(old_hist)) {
        std::filesystem::create_directories(path.parent_path());
        std::filesystem::rename(old_hist, path);
      }
    }
  }

  return path;
}

ChattyConfig default_config(
    const std::unordered_map<agt::Provider, agt::ProviderConfig>& providers) {
  auto it = std::min_element(providers.begin(), providers.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
  return {it->first, it->second.models[0].id, "low"};
}
