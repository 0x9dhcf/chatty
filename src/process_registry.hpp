#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <random>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

struct ProcessHandle {
  std::string handle;
  std::string command;
  pid_t pid = -1;
  std::filesystem::path log_path;
  std::chrono::system_clock::time_point started_at;
  std::optional<int> exit_code;  // set when reap() observes the child exited
};

class ProcessRegistry {
public:
  static constexpr std::size_t kLogCap = 1024 * 1024;  // 1 MiB

  // Register a freshly-forked child. Returns the new handle string.
  std::string register_started(pid_t pid, std::string command,
                               std::filesystem::path log_path) {
    std::lock_guard<std::mutex> g(mu_);
    auto h = make_handle();
    handles_.emplace(h, ProcessHandle{
                            .handle = h,
                            .command = std::move(command),
                            .pid = pid,
                            .log_path = std::move(log_path),
                            .started_at = std::chrono::system_clock::now(),
                            .exit_code = std::nullopt,
                        });
    return h;
  }

  std::optional<ProcessHandle> get(const std::string& handle) {
    std::lock_guard<std::mutex> g(mu_);
    auto it = handles_.find(handle);
    if (it == handles_.end()) return std::nullopt;
    return it->second;
  }

  // Snapshot the registry. Reaps first so callers see fresh exit_codes.
  std::vector<ProcessHandle> list() {
    std::lock_guard<std::mutex> g(mu_);
    reap_all_unlocked();
    std::vector<ProcessHandle> out;
    out.reserve(handles_.size());
    for (auto& [_, h] : handles_) out.push_back(h);
    return out;
  }

  // Number of registered processes that haven't been observed exited.
  // Reaps first so a recently-exited process isn't counted as live.
  std::size_t live_count() {
    std::lock_guard<std::mutex> g(mu_);
    reap_all_unlocked();
    std::size_t n = 0;
    for (auto& [_, h] : handles_)
      if (!h.exit_code) ++n;
    return n;
  }

  // waitpid WNOHANG every alive pid; sets exit_code on those that exited.
  void reap_all() {
    std::lock_guard<std::mutex> g(mu_);
    reap_all_unlocked();
  }

  // Send `sig` to the registered pid. Returns false on no-such-handle or
  // already-exited; true if the kill syscall succeeded.
  bool kill(const std::string& handle, int sig) {
    std::lock_guard<std::mutex> g(mu_);
    auto it = handles_.find(handle);
    if (it == handles_.end()) return false;
    if (it->second.exit_code) return false;
    return ::kill(it->second.pid, sig) == 0;
  }

  // SIGTERM all live; brief wait; SIGKILL stragglers; reap. Used at chatty
  // shutdown so supervised children die with us.
  void shutdown_kill_all() noexcept {
    std::vector<pid_t> live;
    {
      std::lock_guard<std::mutex> g(mu_);
      for (auto& [_, h] : handles_)
        if (!h.exit_code && h.pid > 0) live.push_back(h.pid);
    }
    for (auto pid : live) ::kill(pid, SIGTERM);
    if (live.empty()) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    for (auto pid : live) {
      int status = 0;
      if (::waitpid(pid, &status, WNOHANG) == 0) {
        ::kill(pid, SIGKILL);
        ::waitpid(pid, &status, 0);
      }
    }
  }

  // Lazy log-cap enforcement. Truncate-head: if the log exceeds kLogCap,
  // rewrite it to keep just the last kLogCap bytes (rounded up to the
  // next \n so we don't slice a line).
  static void truncate_log_if_needed(const std::filesystem::path& log) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(log, ec);
    if (ec || sz <= kLogCap) return;

    std::ifstream in(log, std::ios::binary);
    if (!in) return;
    in.seekg(static_cast<std::streamoff>(sz - kLogCap));
    std::string discard;
    std::getline(in, discard);  // align to next \n boundary

    std::ostringstream buf;
    buf << in.rdbuf();
    in.close();

    auto tmp = log;
    tmp += ".trunc.tmp";
    {
      std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
      if (!out) return;
      out << buf.str();
    }
    std::error_code ec2;
    std::filesystem::rename(tmp, log, ec2);
    if (ec2) std::filesystem::remove(tmp, ec);
  }

private:
  std::string make_handle() {
    // Caller already holds mu_.
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<unsigned> dist(0, 0xFFFFu);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "p_%04x", dist(rng));
    std::string h(buf);
    // Extremely unlikely collision — bump until unique.
    while (handles_.contains(h)) {
      std::snprintf(buf, sizeof(buf), "p_%04x", dist(rng));
      h = buf;
    }
    return h;
  }

  // Caller must hold mu_.
  void reap_all_unlocked() {
    for (auto& [_, h] : handles_) {
      if (h.exit_code || h.pid <= 0) continue;
      int status = 0;
      pid_t r = ::waitpid(h.pid, &status, WNOHANG);
      if (r == h.pid) {
        if (WIFEXITED(status))
          h.exit_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
          h.exit_code = 128 + WTERMSIG(status);
        else
          h.exit_code = -1;
      }
    }
  }

  mutable std::mutex mu_;
  std::unordered_map<std::string, ProcessHandle> handles_;
};
