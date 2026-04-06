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
  auto path = *home / "AppData" / "Roaming" / "chatty";
#else
  auto path = *home / ".config" / "chatty";
#endif

  if (!std::filesystem::exists(path.parent_path())) {
    std::filesystem::create_directories(path.parent_path());
  }

  return path;
}

std::filesystem::path data_dir() {
  const auto home = home_dir();
  if (!home)
    throw std::runtime_error("Cannot determine home directory");
#ifdef _WIN32
  auto path = return *home / "AppData" / "Local" / "chatty";
#else
  auto path = *home / ".local" / "state" / "chatty";
#endif

  if (!std::filesystem::exists(path.parent_path())) {
    std::filesystem::create_directories(path.parent_path());
  }

  return path;
}

std::filesystem::path history_path() {
  return data_dir() / "history";
}

ChattyConfig
default_config(const std::unordered_map<agt::Provider, agt::ProviderConfig>& providers) {
  auto it = std::min_element(providers.begin(), providers.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
  return {it->first, it->second.models[0].id, "low"};
}
