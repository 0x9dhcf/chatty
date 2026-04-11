#include "paths.hpp"

#include <pwd.h>
#include <unistd.h>

[[nodiscard]] static std::optional<std::filesystem::path> home_dir() noexcept {
  if (const struct passwd* pw = getpwuid(getuid()))
    return std::filesystem::path(pw->pw_dir);
  if (const char* home = std::getenv("HOME"))
    return std::filesystem::path(home);
  return std::nullopt;
}

std::filesystem::path config_dir() {
  const auto home = home_dir();
  if (!home)
    throw std::runtime_error("Cannot determine home directory");
  auto path = *home / ".config" / "chatty";

  std::filesystem::create_directories(path);
  return path;
}

std::filesystem::path states_dir() {
  const auto home = home_dir();
  if (!home)
    throw std::runtime_error("Cannot determine home directory");
  auto path = *home / ".local" / "state" / "chatty";

  std::filesystem::create_directories(path);
  return path;
}

std::filesystem::path briefs_dir() {
  const auto cfg_dir = config_dir();
  auto path = cfg_dir / "briefs";

  std::filesystem::create_directories(path);
  return path;
}

std::filesystem::path setting_path() {
  return config_dir() / "settings.toml";
}

std::filesystem::path history_path() {
  return states_dir() / "history";
}
