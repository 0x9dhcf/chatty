#pragma once

#include <agt/mcp.hpp>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <toml++/toml.hpp>
#include <vector>
#include <wordexp.h>

// Parse ~/.config/chatty/mcp.toml into a list of agt::mcp_config.
//
// File format (missing file → empty result):
//
//   [[server]]
//   name = "filesystem"
//   transport = "stdio"            # "stdio" | "http"
//   command = "/usr/bin/mcp-fs"    # path for stdio, URL for http
//   args = ["--root", "$HOME/notes"]
//
// Shell-style expansion (~, $HOME, ...) is applied to `command` and each
// `args` entry via wordexp(WRDE_NOCMD); command substitution is disabled.
inline std::vector<agt::mcp_config> load_mcp_configs(const std::filesystem::path& path) {
  std::vector<agt::mcp_config> out;
  if (!std::filesystem::exists(path))
    return out;

  toml::table tbl;
  try {
    tbl = toml::parse_file(path.string());
  } catch (const toml::parse_error& e) {
    throw std::runtime_error("failed to parse mcp config: " + std::string(e.what()));
  }

  auto expand = [](const std::string& s) -> std::string {
    wordexp_t w;
    if (wordexp(s.c_str(), &w, WRDE_NOCMD) != 0)
      return s;
    std::string r = (w.we_wordc > 0) ? w.we_wordv[0] : s;
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

    agt::mcp_config cfg;

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

    out.push_back(std::move(cfg));
  }

  return out;
}
