#pragma once

#include "exec.hpp"
#include <agt/tool.hpp>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <unistd.h>

class FileEdit : public agt::Tool {
  const char* name() const noexcept override { return "file_edit"; }
  const char* description() const noexcept override {
    return "Find and replace exact text in an existing file. Targets text "
           "by content, not byte offset. Requires 'old' to be unique in the "
           "file unless 'replace_all' is true. Prefer this over file_write+pos "
           "for surgical edits — clearer intent, no offset arithmetic, fails "
           "loudly on ambiguity.";
  }

  agt::Json parameters() const override {
    return {{"type", "object"},
            {"properties",
             {{"path", {{"type", "string"},
                        {"description", "path to the file to edit"}}},
              {"old", {{"type", "string"},
                       {"description", "exact text to find (must be unique unless "
                                       "replace_all=true; multi-line OK; case-sensitive "
                                       "byte match)"}}},
              {"new", {{"type", "string"},
                       {"description", "replacement text (may be empty to delete the "
                                       "matched text)"}}},
              {"replace_all", {{"type", "boolean"},
                               {"description", "if true, replace every occurrence; "
                                               "default false (requires exactly one match)"}}}}},
            {"required", {"path", "old", "new"}}};
  }

  agt::Json execute(const agt::Json& input, void* context = nullptr) override {
    (void)context;

    std::filesystem::path p = expand_path(input["path"].get<std::string>());

    if (!std::filesystem::exists(p))
      return "error: file not found: " + p.string() +
             " (use file_write to create new files)";
    if (std::filesystem::is_directory(p))
      return "error: path is a directory, not a file: " + p.string();

    const auto& old_str = input["old"].get_ref<const std::string&>();
    if (old_str.empty())
      return std::string("error: 'old' must not be empty");

    const auto& new_str = input["new"].get_ref<const std::string&>();
    bool replace_all = input.value("replace_all", false);

    // Resolve symlinks so atomic rename overwrites the actual file rather
    // than replacing the link with a regular file.
    std::filesystem::path target;
    try {
      target = std::filesystem::canonical(p);
    } catch (const std::exception& e) {
      return std::string("error: can't resolve path: ") + e.what();
    }

    // Read full file
    std::string content;
    {
      std::ifstream in(target, std::ios::binary);
      if (!in)
        return "error: can't open file for read: " + target.string();
      std::ostringstream buf;
      buf << in.rdbuf();
      content = buf.str();
    }

    // Count occurrences of old_str (literal, byte-exact, non-overlapping).
    std::size_t count = 0;
    {
      std::size_t pos = 0;
      while ((pos = content.find(old_str, pos)) != std::string::npos) {
        ++count;
        pos += old_str.size();
      }
    }

    if (count == 0)
      return "error: 'old' string not found in " + target.string() +
             ". Verify exact whitespace and line endings; widen context if "
             "the snippet is too short to be unique.";
    if (count > 1 && !replace_all)
      return "error: " + std::to_string(count) + " occurrences of 'old' found in " +
             target.string() + "; expected exactly 1. Either widen the 'old' snippet "
             "to include enough surrounding context to be unique, or pass "
             "replace_all=true to replace every occurrence.";

    // Build new content. Advance past 'new' after each replacement so a 'new'
    // that contains 'old' doesn't get re-matched.
    std::string out;
    out.reserve(content.size() + (new_str.size() > old_str.size()
                                      ? (new_str.size() - old_str.size()) * count
                                      : 0));
    std::size_t replaced = 0;
    std::size_t pos = 0;
    for (;;) {
      auto found = content.find(old_str, pos);
      if (found == std::string::npos) {
        out.append(content, pos, std::string::npos);
        break;
      }
      out.append(content, pos, found - pos);
      out.append(new_str);
      pos = found + old_str.size();
      ++replaced;
      if (!replace_all) {
        out.append(content, pos, std::string::npos);
        break;
      }
    }

    // Atomic write: temp in same dir, then rename. PID-suffixed to avoid
    // collisions across concurrent chatty instances.
    auto tmp = target;
    tmp += ".chatty.tmp." + std::to_string(::getpid());

    auto cleanup_tmp = [&] {
      std::error_code ec;
      std::filesystem::remove(tmp, ec);
    };

    try {
      {
        std::ofstream w(tmp, std::ios::binary | std::ios::trunc);
        if (!w) {
          cleanup_tmp();
          return "error: can't create temp file: " + tmp.string();
        }
        w.write(out.data(), static_cast<std::streamsize>(out.size()));
        if (!w) {
          cleanup_tmp();
          return "error: write to temp file failed: " + tmp.string();
        }
      }
      std::filesystem::permissions(tmp,
                                   std::filesystem::status(target).permissions());
      std::filesystem::rename(tmp, target);
    } catch (const std::exception& e) {
      cleanup_tmp();
      return std::string("error: atomic write failed: ") + e.what();
    }

    return "ok: replaced " + std::to_string(replaced) +
           (replaced == 1 ? " occurrence" : " occurrences");
  }
};
