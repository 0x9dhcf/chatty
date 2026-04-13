#pragma once

#include <agt/mcp.hpp>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <toml++/toml.hpp>
#include <vector>
#include <wordexp.h>

// chatty-side per-server entry: the agt config plus knobs that only chatty
// cares about (e.g. whether to bypass the approval prompt for this server's
// tools).
struct McpServerConfig {
  agt::mcp_config server;
  bool auto_approve = false;
};

// Parse ~/.config/chatty/mcp.toml into a list of McpServerConfig.
//
// File format (missing file → empty result):
//
//   [[server]]
//   name = "filesystem"
//   transport = "stdio"            # "stdio" | "http"
//   command = "/usr/bin/mcp-fs"    # path for stdio, URL for http
//   args = ["--root", "$HOME/notes"]
//   auto_approve = false           # optional, default false
//
//   [server.headers]               # optional, http transport only
//   Authorization = "Bearer $GITHUB_PERSONAL_ACCESS_TOKEN"
//   X-Api-Key     = "$SOMETHING"
//
// Shell-style expansion (~, $HOME, ...) is applied to `command`, each
// `args` entry, and each header value via wordexp(WRDE_NOCMD); command
// substitution is disabled.
inline std::vector<McpServerConfig> load_mcp_configs(const std::filesystem::path& path) {
  std::vector<McpServerConfig> out;
  if (!std::filesystem::exists(path))
    return out;

  toml::table tbl;
  try {
    tbl = toml::parse_file(path.string());
  } catch (const toml::parse_error& e) {
    throw std::runtime_error("failed to parse mcp config: " + std::string(e.what()));
  }

  // Expand env vars / ~ in `s`. wordexp splits on whitespace, so we rejoin
  // with single spaces — this preserves multi-word header values like
  // "Bearer $TOKEN" while still working for single-word paths.
  auto expand = [](const std::string& s) -> std::string {
    wordexp_t w;
    if (wordexp(s.c_str(), &w, WRDE_NOCMD) != 0)
      return s;
    std::string r;
    for (std::size_t i = 0; i < w.we_wordc; ++i) {
      if (i > 0)
        r += ' ';
      r += w.we_wordv[i];
    }
    wordfree(&w);
    return r;
  };

  const auto* servers = tbl["server"].as_array();
  if (servers == nullptr)
    return out;

  for (const auto& node : *servers) {
    const auto* t = node.as_table();
    if (t == nullptr)
      continue;

    McpServerConfig entry;
    auto& cfg = entry.server;

    auto name = (*t)["name"].value<std::string>();
    if (!name)
      throw std::runtime_error("mcp config: every [[server]] needs a 'name'");
    cfg.name = *name;

    auto transport = (*t)["transport"].value<std::string>();
    if (!transport)
      throw std::runtime_error("mcp config: server '" + cfg.name + "' missing 'transport'");
    if (*transport == "stdio")
      cfg.transport = agt::McpTransport::stdio;
    else if (*transport == "http")
      cfg.transport = agt::McpTransport::http;
    else
      throw std::runtime_error("mcp config: server '" + cfg.name + "' has invalid transport '" +
                               *transport + "' (stdio|http)");

    auto command = (*t)["command"].value<std::string>();
    if (!command)
      throw std::runtime_error("mcp config: server '" + cfg.name + "' missing 'command'");
    cfg.command = expand(*command);

    if (const auto* args = (*t)["args"].as_array()) {
      for (const auto& a : *args) {
        if (auto s = a.value<std::string>())
          cfg.args.push_back(expand(*s));
      }
    }

    if (const auto* hdrs = (*t)["headers"].as_table()) {
      for (const auto& [k, v] : *hdrs) {
        if (auto s = v.value<std::string>())
          cfg.headers.emplace_back(std::string{k.str()}, expand(*s));
      }
    }

    entry.auto_approve = (*t)["auto_approve"].value_or(false);

    out.push_back(std::move(entry));
  }

  return out;
}
