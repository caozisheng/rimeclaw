// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#ifdef _WIN32

#include <chrono>
#include <cstdlib>
#include <sstream>
#include <thread>
#include <vector>

#include "rimeclaw/common/defer.hpp"
#include "rimeclaw/platform/process.hpp"

#include <spdlog/spdlog.h>

// clang-format off
#include <windows.h>  // must precede psapi.h
#include <psapi.h>
// clang-format on

namespace rimeclaw::platform {

ProcessId spawn_process(const std::vector<std::string>& args,
                        const std::vector<std::string>& env,
                        const std::string& working_dir) {
  if (args.empty())
    return kInvalidPid;

  // Build command line
  std::ostringstream cmdline;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i > 0)
      cmdline << " ";
    // Quote args that contain spaces
    if (args[i].find(' ') != std::string::npos) {
      cmdline << "\"" << args[i] << "\"";
    } else {
      cmdline << args[i];
    }
  }
  std::string cmd_str = cmdline.str();

  // Build environment block if env vars specified
  std::string env_block;
  if (!env.empty()) {
    // Get current environment
    char* current_env = GetEnvironmentStringsA();
    if (current_env) {
      const char* p = current_env;
      while (*p) {
        std::string entry(p);
        env_block += entry;
        env_block += '\0';
        p += entry.size() + 1;
      }
      FreeEnvironmentStringsA(current_env);
    }
    for (const auto& e : env) {
      env_block += e;
      env_block += '\0';
    }
    env_block += '\0';
  }

  // CreateProcessA may modify the command-line buffer; use mutable copies.
  std::vector<char> cmd_buf(cmd_str.begin(), cmd_str.end());
  cmd_buf.push_back('\0');
  std::vector<char> env_buf;
  if (!env_block.empty()) {
    env_buf.assign(env_block.begin(), env_block.end());
  }

  STARTUPINFOA si = {};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi = {};

  BOOL ok = CreateProcessA(
      nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, env.empty() ? 0 : 0,
      env.empty() ? nullptr : env_buf.data(),
      working_dir.empty() ? nullptr : working_dir.c_str(), &si, &pi);

  if (!ok) {
    spdlog::warn("spawn_process failed: {}", GetLastError());
    return kInvalidPid;
  }

  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return pi.dwProcessId;
}

bool is_process_alive(ProcessId pid) {
  if (pid == 0)
    return false;
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h)
    return false;
  DWORD exit_code;
  BOOL ok = GetExitCodeProcess(h, &exit_code);
  CloseHandle(h);
  return ok && exit_code == STILL_ACTIVE;
}

void terminate_process(ProcessId pid) {
  HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
  if (h) {
    TerminateProcess(h, 1);
    CloseHandle(h);
  }
}

void kill_process(ProcessId pid) {
  terminate_process(pid);  // Windows has no SIGKILL equivalent
}

void reload_process(ProcessId /*pid*/) {
  // No-op on Windows (no SIGHUP equivalent)
}

int wait_process(ProcessId pid, int timeout_ms) {
  HANDLE h =
      OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h)
    return -1;

  DWORD wait_time = (timeout_ms < 0)    ? INFINITE
                    : (timeout_ms == 0) ? 0
                                        : static_cast<DWORD>(timeout_ms);

  DWORD wait_result = WaitForSingleObject(h, wait_time);
  if (wait_result != WAIT_OBJECT_0) {
    CloseHandle(h);
    return -1;
  }

  DWORD exit_code;
  GetExitCodeProcess(h, &exit_code);
  CloseHandle(h);
  return static_cast<int>(exit_code);
}

// Locate a bash.exe (Git for Windows) for the "bash" tool.
// Caches the result after the first call.
static const std::string& find_bash_exe() {
  static std::string cached = [] {
    // 1. Check PATH first
    const char* path_env = std::getenv("PATH");
    if (path_env) {
      std::istringstream paths(path_env);
      std::string dir;
      while (std::getline(paths, dir, ';')) {
        if (dir.empty()) continue;
        std::string candidate = dir + "\\bash.exe";
        DWORD attrs = GetFileAttributesA(candidate.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES &&
            !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
          return candidate;
        }
      }
    }
    // 2. Common Git for Windows locations
    const char* candidates[] = {
      "C:\\Program Files\\Git\\bin\\bash.exe",
      "C:\\Program Files (x86)\\Git\\bin\\bash.exe",
    };
    for (const char* c : candidates) {
      DWORD attrs = GetFileAttributesA(c);
      if (attrs != INVALID_FILE_ATTRIBUTES &&
          !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return std::string(c);
      }
    }
    return std::string();  // not found — fall back to cmd.exe
  }();
  return cached;
}

ExecResult exec_capture(const std::string& command, int timeout_seconds,
                        const std::string& working_dir) {
  ExecResult result;

  // Create a pipe for the child's stdout+stderr.
  SECURITY_ATTRIBUTES sa = {};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE read_pipe = nullptr;
  HANDLE write_pipe = nullptr;
  if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
    result.exit_code = -1;
    return result;
  }
  // Ensure the read end is not inherited.
  SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

  // Build command line: prefer bash.exe (Git for Windows) so the LLM can
  // emit standard Unix shell syntax.  Fall back to cmd.exe if bash is not
  // found.
  std::string cmd_line;
  const auto& bash = find_bash_exe();
  if (!bash.empty()) {
    // bash -c expects a single string argument.
    // Escape internal double-quotes in the command.
    std::string escaped;
    escaped.reserve(command.size());
    for (char ch : command) {
      if (ch == '"') escaped += '\\';
      escaped += ch;
    }
    cmd_line = "\"" + bash + "\" -c \"" + escaped + "\"";
  } else {
    cmd_line = "cmd /c " + command;
  }
  std::vector<char> cmd_buf(cmd_line.begin(), cmd_line.end());
  cmd_buf.push_back('\0');

  STARTUPINFOA si = {};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = write_pipe;
  si.hStdError = write_pipe;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  PROCESS_INFORMATION pi = {};
  BOOL ok = CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr,
                           TRUE,  // inherit handles
                           CREATE_NO_WINDOW, nullptr,
                           working_dir.empty() ? nullptr : working_dir.c_str(),
                           &si, &pi);

  CloseHandle(write_pipe);  // parent closes write end

  if (!ok) {
    CloseHandle(read_pipe);
    result.exit_code = -1;
    return result;
  }

  // RAII cleanup for handles.
  DEFER({
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  });
  DEFER(CloseHandle(read_pipe));

  // Read output with timeout awareness.
  DWORD wait_ms = (timeout_seconds > 0)
                      ? static_cast<DWORD>(timeout_seconds) * 1000
                      : INFINITE;
  auto start = std::chrono::steady_clock::now();

  char buffer[1024];
  bool timed_out = false;

  for (;;) {
    DWORD remaining = INFINITE;
    if (timeout_seconds > 0) {
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start);
      if (elapsed.count() >= static_cast<long long>(wait_ms)) {
        timed_out = true;
        break;
      }
      remaining = static_cast<DWORD>(wait_ms - elapsed.count());
    }

    // Check if there is data or if process ended.
    DWORD avail = 0;
    if (!PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &avail, nullptr)) {
      DWORD wr =
          WaitForSingleObject(pi.hProcess, remaining < 100 ? remaining : 100);
      if (wr == WAIT_OBJECT_0) {
        break;
      }
      continue;
    }
    if (avail == 0) {
      DWORD wr =
          WaitForSingleObject(pi.hProcess, remaining < 100 ? remaining : 100);
      if (wr == WAIT_OBJECT_0) {
        // Process exited; drain remaining output.
        DWORD bytes_read = 0;
        while (ReadFile(read_pipe, buffer, sizeof(buffer) - 1, &bytes_read,
                        nullptr) &&
               bytes_read > 0) {
          buffer[bytes_read] = '\0';
          result.output += buffer;
        }
        break;
      }
      continue;
    }

    DWORD bytes_read = 0;
    if (!ReadFile(read_pipe, buffer, sizeof(buffer) - 1, &bytes_read,
                  nullptr) ||
        bytes_read == 0) {
      DWORD wr =
          WaitForSingleObject(pi.hProcess, remaining < 100 ? remaining : 100);
      if (wr == WAIT_OBJECT_0) {
        break;
      }
      continue;
    }
    buffer[bytes_read] = '\0';
    result.output += buffer;
  }

  if (timed_out) {
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 3000);
    result.exit_code = -2;
    return result;
  }

  // Wait for process to fully exit before reading exit code.
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  result.exit_code = static_cast<int>(exit_code);
  return result;
}

std::string executable_path() {
  char buf[MAX_PATH];
  DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  if (len > 0 && len < MAX_PATH) {
    return std::string(buf, len);
  }
  return "rimeclaw.exe";
}

std::string home_directory() {
  const char* userprofile = std::getenv("USERPROFILE");
  if (userprofile)
    return userprofile;
  const char* homedrive = std::getenv("HOMEDRIVE");
  const char* homepath = std::getenv("HOMEPATH");
  if (homedrive && homepath)
    return std::string(homedrive) + homepath;
  return "C:\\";
}

}  // namespace rimeclaw::platform

#endif  // _WIN32
