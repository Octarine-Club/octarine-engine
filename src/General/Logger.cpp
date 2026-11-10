#include "Logger.h"

#include <spdlog/logger.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <deque>
#include <memory>
#include <mutex>

#if defined(__ANDROID__)
#include <spdlog/sinks/android_sink.h>
#else
#include <spdlog/sinks/stdout_color_sinks.h>
#endif

// iOS os_log sink is parked on the defer/ios branch pending an Apple Developer account.

namespace {

std::shared_ptr<spdlog::logger> g_lua_logger;
std::deque<std::string> g_history;
std::mutex g_history_mutex;
std::vector<Logger::ScriptError> g_script_errors;
std::uint64_t g_script_error_sequence = 0;

void PushHistory(std::string entry) {
  // Cap history to prevent unbounded growth from per-frame logging.
  constexpr size_t kMaxHistory = 1000;
  std::lock_guard<std::mutex> lock(g_history_mutex);
  if (g_history.size() >= kMaxHistory) {
    g_history.pop_front();
  }
  g_history.push_back(std::move(entry));
}

// Parses log level string, returning spdlog::level::n_levels on invalid input.
spdlog::level::level_enum ParseLevelName(std::string name) {
  std::transform(name.begin(), name.end(), name.begin(),
                 [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  if (name == "trace") return spdlog::level::trace;
  if (name == "debug") return spdlog::level::debug;
  if (name == "info") return spdlog::level::info;
  if (name == "warn" || name == "warning") return spdlog::level::warn;
  if (name == "error" || name == "err") return spdlog::level::err;
  if (name == "critical" || name == "crit") return spdlog::level::critical;
  if (name == "off") return spdlog::level::off;
  return spdlog::level::n_levels;
}

}  // namespace

void Logger::Init() {
#if defined(__ANDROID__)
  // Android logcat sinks for engine and Lua messages.
  auto main_logger = spdlog::android_logger_mt("octarine", "Octarine");
  spdlog::set_default_logger(main_logger);
  g_lua_logger = spdlog::android_logger_mt("octarine-lua", "OctarineLua");
#else
  // Colored stdout sinks for desktop platforms.
  auto main_logger = spdlog::stdout_color_mt("octarine");
  spdlog::set_default_logger(main_logger);
  g_lua_logger = spdlog::stdout_color_mt("lua");
#endif

  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
  g_lua_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [lua] %v");

#ifdef OCTARINE_LOG_LEVEL_NAME
  SetLevel(OCTARINE_LOG_LEVEL_NAME);
#endif
}

void Logger::SetLevel(const std::string& level) {
  const auto parsed = ParseLevelName(level);
  if (parsed == spdlog::level::n_levels) {
    Logger::Warn("Unknown log level '" + level +
                 "' — expected trace|debug|info|warn|error|critical|off. Keeping current.");
    return;
  }
  spdlog::set_level(parsed);
  if (g_lua_logger) {
    g_lua_logger->set_level(parsed);
  }
}

void Logger::Error(const std::string& message) {
  spdlog::error(message);
  PushHistory("[Error] " + message);
}

void Logger::Warn(const std::string& message) {
  spdlog::warn(message);
  PushHistory("[Warn] " + message);
}

#ifndef OCTARINE_SHIPPED
void Logger::Info(const std::string& message) {
  spdlog::info(message);
  PushHistory("[Info] " + message);
}
#endif

void Logger::LogLua(const std::string& message) {
  if (g_lua_logger) {
    g_lua_logger->info(message);
  } else {
    spdlog::info(message);
  }
  PushHistory(message);
}

void Logger::ErrorLua(const std::string& message) {
  if (g_lua_logger) {
    g_lua_logger->error(message);
  } else {
    spdlog::error(message);
  }
  PushHistory("[Error] [Lua] " + message);
  // Cap toast history ring buffer.
  constexpr size_t kMaxScriptErrors = 8;
  std::lock_guard<std::mutex> lock(g_history_mutex);
  g_script_errors.push_back({++g_script_error_sequence, message, std::chrono::steady_clock::now()});
  if (g_script_errors.size() > kMaxScriptErrors) {
    g_script_errors.erase(g_script_errors.begin());
  }
}

void Logger::WarnLua(const std::string& message) {
  if (g_lua_logger) {
    g_lua_logger->warn(message);
  } else {
    spdlog::warn(message);
  }
  PushHistory("[Warn] [Lua] " + message);
}

#ifndef OCTARINE_SHIPPED
void Logger::InfoLua(const std::string& message) {
  if (g_lua_logger) {
    g_lua_logger->info(message);
  } else {
    spdlog::info(message);
  }
  PushHistory("[Info] [Lua] " + message);
}
#endif

std::vector<std::string> Logger::GetHistory() {
  std::lock_guard<std::mutex> lock(g_history_mutex);
  return {g_history.begin(), g_history.end()};
}

void Logger::ForEachHistory(const std::function<void(const std::string&)>& callback) {
  std::lock_guard<std::mutex> lock(g_history_mutex);
  for (const auto& line : g_history) {
    callback(line);
  }
}

void Logger::ClearHistory() {
  std::lock_guard<std::mutex> lock(g_history_mutex);
  g_history.clear();
}

std::vector<Logger::ScriptError> Logger::RecentScriptErrors() {
  std::lock_guard<std::mutex> lock(g_history_mutex);
  return g_script_errors;
}
