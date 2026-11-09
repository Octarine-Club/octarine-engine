#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class Logger {
 public:
  // Captured Lua script error for debug toast notifications.
  struct ScriptError {
    std::uint64_t sequence{};
    std::string message;
    std::chrono::steady_clock::time_point when{};
  };

 public:
  static void Init();
  // Sets runtime log level (trace|debug|info|warn|error|critical|off).
  static void SetLevel(const std::string& level);
  static void Error(const std::string& message);
  static void Warn(const std::string& message);
#ifdef OCTARINE_SHIPPED
  inline static void Info(const std::string& /*message*/) noexcept {}
  inline static void InfoLua(const std::string& /*message*/) noexcept {}
#else
  static void Info(const std::string& message);
  static void InfoLua(const std::string& message);
#endif
  static void LogLua(const std::string& message);
  static void ErrorLua(const std::string& message);
  static void WarnLua(const std::string& message);

  static std::vector<std::string> GetHistory();
  static void ForEachHistory(const std::function<void(const std::string&)>& callback);
  static void ClearHistory();

  // Returns snapshot of recent script errors for debug overlay.
  static std::vector<ScriptError> RecentScriptErrors();
};
