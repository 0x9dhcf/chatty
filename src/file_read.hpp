#pragma once

#include "agt/tool.hpp"
#include "exec.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

class FileRead : public agt::Tool {
  const char *name() const noexcept override { return "file_read"; }
  const char *description() const noexcept override { return "read from a file"; }

  agt::Json parameters() const override {
    return {
        {"type", "object"},
        {"properties",
         {{"path", {{"type", "string"}, {"description", "the path of the file to read"}}},
          {"pos", {{"type", "integer"}, {"description", "byte offset to start reading from"}}},
          {"len", {{"type", "integer"}, {"description", "number of bytes to read"}}}}},
        {"required", {"path"}}};
  }

  agt::Json execute(const agt::Json &input, void *context = nullptr) override {
    (void)context;
    std::filesystem::path p = expand_path(input["path"].get<std::string>());
    std::ifstream in(p, std::ios::binary);
    if (!in)
      return "error: can't open file for read";

    if (input.contains("pos"))
      in.seekg(input["pos"].get<long>());

    if (input.contains("len")) {
      auto len = input["len"].get<long>();
      std::string buf(len, '\0');
      in.read(buf.data(), len);
      buf.resize(in.gcount());
      return buf;
    }

    auto result = std::string(std::istreambuf_iterator<char>(in), {});
    constexpr size_t max_output = 8000;
    if (result.size() > max_output) {
      auto total = result.size();
      result.resize(max_output);
      result += "\n... (output truncated, showing " + std::to_string(max_output) + " of " +
                std::to_string(total) + " bytes, use pos/len to read specific sections)";
    }
    return result;
  }
};
