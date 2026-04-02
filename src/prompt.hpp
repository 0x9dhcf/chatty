// vim: set ts=2 sw=2 sts=2 et:
#pragma once

#include <iostream>
#include <optional>
#include <print>
#include <string>
#include <vector>

namespace prompt {

inline std::optional<std::string> input(const std::string &label) {
  std::print("{}", label);
  std::fflush(stdout);
  std::string line;
  if (!std::getline(std::cin, line))
    return std::nullopt;
  return line;
}

inline std::optional<size_t> choose(const std::string &title,
                                    const std::vector<std::string> &items) {
  if (items.empty())
    return std::nullopt;

  std::println("{}", title);
  for (size_t i = 0; i < items.size(); ++i)
    std::println("  {}) {}", i + 1, items[i]);

  std::print("> ");
  std::fflush(stdout);

  size_t choice = 0;
  if (!(std::cin >> choice) || choice < 1 || choice > items.size()) {
    std::cin.clear();
    std::cin.ignore(10000, '\n');
    return std::nullopt;
  }
  std::cin.ignore(10000, '\n');
  return choice - 1;
}

inline bool confirm(const std::string &msg) {
  std::print("{} [y/N] ", msg);
  std::fflush(stdout);
  std::string line;
  if (!std::getline(std::cin, line) || line.empty())
    return false;
  return line[0] == 'y' || line[0] == 'Y';
}

} // namespace prompt
