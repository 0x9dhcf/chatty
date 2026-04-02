#pragma once

#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <wordexp.h>

inline std::string expand_path(const std::string &path) {
  wordexp_t w;
  if (wordexp(path.c_str(), &w, WRDE_NOCMD) != 0)
    return path;
  std::string result = w.we_wordv[0];
  wordfree(&w);
  return result;
}

inline std::string exec(const std::string &cmd, int timeout_sec = 10) {
  std::string escaped;
  for (char c : cmd) {
    if (c == '\'')
      escaped += "'\\''";
    else
      escaped += c;
  }
  std::string timeout = std::to_string(timeout_sec);
  std::string wrapped = "timeout " + timeout + " sh -c '" + escaped + "' 2>&1";
  std::array<char, 512> buf;
  std::string result;
  std::unique_ptr<FILE, int (*)(FILE *)> pipe(popen(wrapped.c_str(), "r"), pclose);
  if (!pipe)
    throw std::runtime_error("popen() failed");
  while (fgets(buf.data(), buf.size(), pipe.get()) != nullptr)
    result += buf.data();

  // trim the result
  auto start = result.find_first_not_of(" \t\n\r");
  auto end = result.find_last_not_of(" \t\n\r");
  return (start == std::string::npos) ? "" : result.substr(start, end - start + 1);
}
