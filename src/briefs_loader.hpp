#pragma once

#include <filesystem>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_map>

// Read every *.md file in `dir` into a name -> content map, where the name is
// the file's stem. Missing directory yields an empty map; per-file open
// failures throw.
inline std::unordered_map<std::string, std::string>
load_briefs(const std::filesystem::path& dir) {
  std::unordered_map<std::string, std::string> out;
  if (!std::filesystem::exists(dir))
    return out;

  auto mdfiles = std::filesystem::directory_iterator(dir) |
                 std::views::filter([](const std::filesystem::directory_entry& entry) {
                   return entry.is_regular_file() && entry.path().extension() == ".md";
                 });
  for (auto f : mdfiles) {
    auto path = f.path();
    auto name = path.filename().stem().string();
    std::ifstream file(path, std::ios::binary);
    if (!file)
      throw std::runtime_error("Cannot open file: " + path.string());
    std::string content(std::filesystem::file_size(path), '\0');
    file.read(content.data(), static_cast<std::streamsize>(content.size()));
    out[name] = std::move(content);
  }
  return out;
}
