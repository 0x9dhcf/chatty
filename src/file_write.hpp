#pragma once

#include "exec.hpp"
#include <agt/tool.hpp>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>

class FileWrite : public agt::Tool {
  const char* name() const noexcept override { return "file_write"; }
  const char* description() const noexcept override { return "write to a file"; }

  agt::Json parameters() const override {
    return {{"type", "object"},
            {"properties",
             {{"path", {{"type", "string"}, {"description", "the path of the file to write"}}},
              {"data", {{"type", "string"}, {"description", "the data to write"}}},
              {"pos", {{"type", "integer"}, {"description", "byte offset to write at"}}}}},
            {"required", {"path", "data"}}};
  }

  agt::Json execute(const agt::Json& input, void* context = nullptr) override {
    (void)context;
    std::filesystem::path p = expand_path(input["path"].get<std::string>());
    if (std::filesystem::is_directory(p))
      return "error: path is a directory, not a file";
    auto mode = std::ios::binary;

    if (input.contains("pos")) {
      std::fstream out(p, mode | std::ios::in | std::ios::out);
      if (!out)
        return "error: can't open file for write";
      out.seekp(input["pos"].get<long>());
      const auto& data = input["data"].get_ref<const std::string&>();
      out.write(data.data(), static_cast<std::streamsize>(data.size()));
      return "ok";
    }

    std::ofstream out(p, mode);
    if (!out)
      return "error: can't open file for write";
    const auto& data = input["data"].get_ref<const std::string&>();
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return "ok";
  }
};
