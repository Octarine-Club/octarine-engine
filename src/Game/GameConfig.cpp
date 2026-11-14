#include "Game/GameConfig.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_storage.h>
#include <SDL3/SDL_timer.h>

#include <sstream>
#include <unordered_map>

#include "General/AngleUnit.h"
#include "General/Constants.h"
#include "General/IniParser.h"
#include "General/Logger.h"
#include "General/Utils.h"

inline auto ReadConfigFile(void* dst, const Uint64 dstLen) -> std::unordered_map<std::string, std::string> {
  if (dst == nullptr || dstLen == 0) {
    Logger::Warn("Attempted to read an empty or null config file buffer.");
    return {};
  }

  const std::string_view content(static_cast<const char*>(dst), dstLen);
  if (content.empty()) {
    Logger::Warn("Config file content is empty after conversion.");
    return {};
  }

  std::vector<std::string> warnings;
  auto config = octarine::IniParser::ParseFlatMap(content, &warnings);
  for (const auto& w : warnings) {
    Logger::Warn("config.ini: " + w);
  }
  return config;
}

bool GameConfig::LoadConfigFromFile(const std::string& assetPath) {
  const auto storage = SDL_OpenTitleStorage(assetPath.c_str(), 0);

  if (storage == nullptr) {
    Logger::Error("Failed to open title storage: " + assetPath);
    Logger::Error(std::string(SDL_GetError()));
    return false;
  }

  while (!SDL_StorageReady(storage)) {
    SDL_Delay(1);
  }

  Uint64 dstLen = 0;

  if (!SDL_GetStorageFileSize(storage, kConfigFileName, &dstLen)) {
    Logger::Error("Failed to size config file: " + std::string(SDL_GetError()));
    return false;
  }

  if (dstLen == 0) {
    Logger::Error("Config file is empty: " + std::string(kConfigFileName));
    return false;
  }

  void* dst = SDL_malloc(dstLen);
  if (!dst) {
    Logger::Error("SDL_malloc failed for config buffer: " + std::string(SDL_GetError()));
    return false;
  }

  if (!SDL_ReadStorageFile(storage, kConfigFileName, dst, dstLen)) {
    Logger::Error(std::string(SDL_GetError()));
    SDL_free(dst);
    return false;
  }

  const auto config = ReadConfigFile(dst, dstLen);
  SDL_free(dst);

  if (config.empty()) {
    Logger::Error("Failed to read config file: " + std::string(kConfigFileName));
    return false;
  }

  // Create the instance and populate it with the config settings
  SetAssetPath(assetPath);
  has_loaded_config_ = true;
  return LoadConfig(config);
}

bool GameConfig::LoadConfigFromStream(SDL_IOStream* io, const std::string& assetPath) {
  if (io == nullptr) {
    Logger::Error("LoadConfigFromStream called with null stream");
    return false;
  }
  std::size_t dstLen = 0;
  void* dst = SDL_LoadFile_IO(io, &dstLen, true);  // closes io
  if (!dst) {
    Logger::Error("Failed to read config from stream: " + std::string(SDL_GetError()));
    return false;
  }
  const auto config = ReadConfigFile(dst, dstLen);
  SDL_free(dst);

  if (config.empty()) {
    Logger::Error("Failed to parse config stream for: " + assetPath);
    return false;
  }

  SetAssetPath(assetPath);
  has_loaded_config_ = true;
  return LoadConfig(config);
}

bool GameConfig::LoadConfig(const std::unordered_map<std::string, std::string>& settings) {
  bool success = true;

  // LogLevel is parsed first so subsequent config-loading Logger::Info calls already honor it.
  success &= SetValue(settings, "LogLevel", &GameConfig::SetLogLevel, false);
  success &= SetValue(settings, "Title", &GameConfig::SetGameTitle, true);
  success &= SetValue(settings, "StartupScript", &GameConfig::SetStartupScript, true);
  success &= SetValue(settings, "BaseResolutionWidth", &GameConfig::SetBaseWidth, false);
  success &= SetValue(settings, "BaseResolutionHeight", &GameConfig::SetBaseHeight, false);
  success &= SetValue(settings, "MinAspectRatio", &GameConfig::SetMinAspectRatio, false);
  success &= SetValue(settings, "MaxAspectRatio", &GameConfig::SetMaxAspectRatio, false);
  success &= SetValue(settings, "DefaultScalingMode", &GameConfig::SetDefaultScaleMode, false);
  success &= SetValue(settings, "DefaultWindowWidth", &GameConfig::SetDefaultWidth, false);
  success &= SetValue(settings, "DefaultWindowHeight", &GameConfig::SetDefaultHeight, false);
  success &= SetValue(settings, "DefaultFullscreen", &GameConfig::SetDefaultFullscreen, false);
  success &= SetValue(settings, "DefaultWindowMode", &GameConfig::SetDefaultWindowMode, false);
  success &= SetValue(settings, "DefaultPresentationMode", &GameConfig::SetDefaultPresentationMode, false);
  success &= SetValue(settings, "VSync", &GameConfig::SetDefaultVSync, false);
  success &= SetValue(settings, "DefaultVSync", &GameConfig::SetDefaultVSync, false);
  success &= SetValue(settings, "Resizable", &GameConfig::SetWindowResizable, false);
  success &= SetValue(settings, "WindowResizable", &GameConfig::SetWindowResizable, false);
  success &= SetValue(settings, "FpsTarget", &GameConfig::SetFpsTarget, false);
  success &= SetValue(settings, "HotReload", &GameConfig::SetHotReloadEnabled, false);
  success &= SetValue(settings, "HotReloadPollSeconds", &GameConfig::SetHotReloadPollSeconds, false);
  success &= SetValue(settings, "DebugOverlayBackgroundColor", &GameConfig::SetDebugOverlayBackgroundColor, false);
  success &= SetValue(settings, "AngleUnit", &GameConfig::SetAngleUnit, false);
  success &= SetValue(settings, "RenderQueueCapacity", &GameConfig::SetRenderQueueCapacity, false);

  if (base_width_ < 0 || base_height_ < 0) {
    Logger::Error("BaseResolutionWidth and BaseResolutionHeight cannot be negative (got " +
                  std::to_string(base_width_) + "x" + std::to_string(base_height_) + ")");
    success = false;
  }
  if (min_aspect_ratio_ > 0.0F && max_aspect_ratio_ > 0.0F && min_aspect_ratio_ > max_aspect_ratio_) {
    Logger::Error("MinAspectRatio (" + std::to_string(min_aspect_ratio_) + ") cannot be greater than MaxAspectRatio (" +
                  std::to_string(max_aspect_ratio_) + ")");
    success = false;
  }

  // Logged whether or not the key was present: the default is degrees, so a project authored in
  // radians that omits the key would otherwise reinterpret every angle silently.
  octarine::AngleUnits::Set(engine_options_.angleUnit);
  Logger::Info(std::string("Angle unit: ") + (octarine::AngleUnits::IsDegrees() ? "degrees" : "radians"));

  if (success) {
    has_loaded_config_ = true;
  }
  return success;
}

const EngineOptions& GameConfig::GetEngineOptions() const { return engine_options_; }

EngineOptions& GameConfig::GetEngineOptions() { return engine_options_; }

std::string GameConfig::GetAssetPath() const {
  assert(has_loaded_config_);
  return asset_path_;
}

std::string GameConfig::GetGameTitle() const {
  assert(has_loaded_config_);
  return game_title_;
}

std::string GameConfig::GetStartupScript() const {
  assert(has_loaded_config_);
  return startup_script_;
}

std::optional<std::string> GameConfig::GetDefaultScaleMode() const {
  assert(has_loaded_config_);
  return default_scaling_mode_;
}

int GameConfig::GetBaseWidth() const {
  return base_width_ > 0 ? base_width_ : (default_width_ > 0 ? default_width_ : Constants::kDefaultWindowWidth);
}

int GameConfig::GetBaseHeight() const {
  return base_height_ > 0 ? base_height_ : (default_height_ > 0 ? default_height_ : Constants::kDefaultWindowHeight);
}

void GameConfig::SetBaseWidth(const int baseWidth) { base_width_ = baseWidth; }
void GameConfig::SetBaseHeight(const int baseHeight) { base_height_ = baseHeight; }
float GameConfig::GetMinAspectRatio() const { return min_aspect_ratio_; }
void GameConfig::SetMinAspectRatio(const float minAspect) { min_aspect_ratio_ = std::max(0.0F, minAspect); }
float GameConfig::GetMaxAspectRatio() const { return max_aspect_ratio_; }
void GameConfig::SetMaxAspectRatio(const float maxAspect) { max_aspect_ratio_ = std::max(0.0F, maxAspect); }

int GameConfig::GetDefaultWidth() const { return default_width_ > 0 ? default_width_ : GetBaseWidth(); }

int GameConfig::GetDefaultHeight() const { return default_height_ > 0 ? default_height_ : GetBaseHeight(); }

void GameConfig::SetAssetPath(const std::string& assetPath) {
  if (assetPath.empty()) {
    Logger::Warn("Attempted to set empty asset path. Keeping current if any.");
    return;
  }
  asset_path_ = assetPath;
  Logger::Info("Asset path set to: " + asset_path_);
}

void GameConfig::SetGameTitle(const std::string& gameTitle) {
  if (gameTitle.empty()) {
    Logger::Warn("Attempted to set empty game title. Keeping current if any.");
    return;
  }
  game_title_ = gameTitle;
  Logger::Info("Game title set to: " + game_title_);
}

void GameConfig::SetStartupScript(const std::string& startupScript) {
  if (startupScript.empty()) {
    Logger::Warn("Attempted to set empty startup script. Keeping current if any.");
    return;
  }
  startup_script_ = startupScript;
  Logger::Info("Startup script set to: " + startup_script_);
}

void GameConfig::SetDefaultScaleMode(const std::string& defaultScaleMode) {
  if (defaultScaleMode.empty()) {
    default_scaling_mode_ = std::nullopt;
    Logger::Info("Default scaling mode set to not present (empty string provided).");
  } else {
    default_scaling_mode_ = defaultScaleMode;
    Logger::Info("Default scaling mode set to: " + *default_scaling_mode_);
  }
}

void GameConfig::SetDefaultWidth(const int defaultWidth) { default_width_ = defaultWidth; }
void GameConfig::SetDefaultHeight(const int defaultHeight) { default_height_ = defaultHeight; }

void GameConfig::SetDefaultWindowMode(const std::string& windowMode) {
  if (const auto parsed = ParseWindowMode(windowMode)) {
    engine_options_.windowMode = *parsed;
    Logger::Info("Default window mode set to: " + std::string(WindowModeToString(*parsed)));
  } else {
    Logger::Warn("Unknown DefaultWindowMode '" + windowMode +
                 "' (expected windowed|borderless|exclusive); keeping current.");
  }
}

void GameConfig::SetDefaultFullscreen(const bool fullscreen) {
  engine_options_.windowMode = fullscreen ? WindowMode::BorderlessFullscreen : WindowMode::Windowed;
  Logger::Info(std::string("Default fullscreen set to: ") + (fullscreen ? "true (borderless)" : "false (windowed)"));
}

void GameConfig::SetDefaultPresentationMode(const std::string& presentationMode) {
  if (const auto parsed = ParsePresentationMode(presentationMode)) {
    engine_options_.presentationMode = *parsed;
    Logger::Info("Default presentation mode set to: " + std::string(PresentationModeToString(*parsed)));
  } else {
    Logger::Warn("Unknown DefaultPresentationMode '" + presentationMode +
                 "' (expected letterbox|integer_scale|stretch); keeping current.");
  }
}

void GameConfig::SetDefaultVSync(const bool vsync) {
  engine_options_.vsync = vsync;
  Logger::Info(std::string("Default VSync ") + (vsync ? "enabled" : "disabled"));
}

void GameConfig::SetWindowResizable(const bool resizable) {
  engine_options_.windowResizable = resizable;
  Logger::Info(std::string("Window resizable ") + (resizable ? "enabled" : "disabled"));
}

void GameConfig::SetFpsTarget(const int fpsTarget) {
  if (fpsTarget < 0) {
    Logger::Warn("FpsTarget must be >= 0 (0 = uncapped); keeping current value.");
    return;
  }
  engine_options_.fpsTarget = fpsTarget;
  Logger::Info(fpsTarget == 0 ? std::string("FPS target: uncapped") : "FPS target: " + std::to_string(fpsTarget));
}

void GameConfig::SetHotReloadEnabled(const bool enabled) {
  engine_options_.hotReloadEnabled = enabled;
  Logger::Info(std::string("Hot reload ") + (enabled ? "enabled" : "disabled"));
}

// Parses only; LoadConfig applies it to the process-wide AngleUnits once every key is read.
void GameConfig::SetAngleUnit(const std::string& unit) {
  if (unit == "radians" || unit == "rad") {
    engine_options_.angleUnit = octarine::AngleUnit::Radians;
  } else if (unit == "degrees" || unit == "deg") {
    engine_options_.angleUnit = octarine::AngleUnit::Degrees;
  } else {
    Logger::Warn("Unknown AngleUnit '" + unit + "' (expected radians|degrees); keeping current.");
  }
}

void GameConfig::SetDebugOverlayBackgroundColor(const std::string& colorStr) {
  const auto parts = SplitString(colorStr, ',');
  if (parts.size() >= 3) {
    try {
      engine_options_.debugOverlayBackgroundColor.r = static_cast<std::uint8_t>(std::stoi(parts[0]));
      engine_options_.debugOverlayBackgroundColor.g = static_cast<std::uint8_t>(std::stoi(parts[1]));
      engine_options_.debugOverlayBackgroundColor.b = static_cast<std::uint8_t>(std::stoi(parts[2]));
      if (parts.size() >= 4) {
        engine_options_.debugOverlayBackgroundColor.a = static_cast<std::uint8_t>(std::stoi(parts[3]));
      } else {
        engine_options_.debugOverlayBackgroundColor.a = Constants::kUint8Max;
      }
      Logger::Info("Debug overlay background color: " + colorStr);
    } catch (const std::exception& e) {
      Logger::Warn("Failed to parse DebugOverlayBackgroundColor '" + colorStr + "': " + e.what());
    }
  } else {
    Logger::Warn("Invalid format for DebugOverlayBackgroundColor '" + colorStr +
                 "' (expected R,G,B[,A]); keeping current.");
  }
}

void GameConfig::SetHotReloadPollSeconds(const float seconds) {
  if (seconds <= 0.0F) {
    Logger::Warn("HotReloadPollSeconds must be > 0; keeping current value.");
    return;
  }
  engine_options_.hotReloadPollSeconds = seconds;
}

void GameConfig::SetLogLevel(const std::string& logLevel) {
  if (logLevel.empty()) return;
  Logger::SetLevel(logLevel);
  Logger::Info("Log level set to: " + logLevel);
}

void GameConfig::SetRenderQueueCapacity(const int capacity) {
  if (capacity < 1024 || capacity > 1024 * 1024) {
    Logger::Warn("RenderQueueCapacity must be in [1024, 1048576]; keeping current value.");
    return;
  }
  engine_options_.renderQueueCapacity = static_cast<size_t>(capacity);
  Logger::Info("RenderQueue capacity set to: " + std::to_string(capacity));
}
