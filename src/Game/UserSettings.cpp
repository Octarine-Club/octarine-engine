#include "Game/UserSettings.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "General/FileSandbox.h"
#include "General/IniParser.h"
#include "General/Logger.h"

namespace octarine {

namespace {

struct WindowRect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

void ApplyFullscreenResAndPresentationKeys(UserSettings& s, const std::string& key, const std::string& val) {
  if (key == "fullscreen_width" || key == "fullscreenWidth") {
    s.fullscreenWidth = std::max(0, std::stoi(val));
  } else if (key == "fullscreen_height" || key == "fullscreenHeight") {
    s.fullscreenHeight = std::max(0, std::stoi(val));
  } else if (key == "fullscreen_refresh_rate" || key == "fullscreenRefreshRate" || key == "refresh_rate") {
    s.fullscreenRefreshRate = std::max(0.0F, std::stof(val));
  } else if (key == "vsync") {
    s.vsync = IniParser::ParseBool(val);
  } else if (key == "resizable" || key == "window_resizable" || key == "windowResizable") {
    s.windowResizable = IniParser::ParseBool(val);
  }
}

void ApplyWindowModeAndDisplayKeys(UserSettings& s, const std::string& key, const std::string& val) {
  if (key == "mode" || key == "window_mode" || key == "windowMode") {
    if (const auto parsed = ParseWindowMode(val)) {
      s.SetWindowMode(*parsed);
    }
    return;
  }
  if (key == "presentation_mode" || key == "presentationMode") {
    if (const auto parsed = ParsePresentationMode(val)) {
      s.presentationMode = *parsed;
    }
    return;
  }
  ApplyFullscreenResAndPresentationKeys(s, key, val);
}

void ApplyWindowPlacementKeys(UserSettings& s, const std::string& key, const std::string& val, bool& handled) {
  handled = true;
  if (key == "x") {
    s.windowX = std::stoi(val);
    s.hasWindowPlacement = true;
  } else if (key == "y") {
    s.windowY = std::stoi(val);
    s.hasWindowPlacement = true;
  } else if (key == "width") {
    s.windowWidth = std::stoi(val);
  } else if (key == "height") {
    s.windowHeight = std::stoi(val);
  } else if (key == "display_index" || key == "displayIndex") {
    s.displayIndex = std::stoi(val);
  } else if (key == "display_name" || key == "displayName") {
    s.displayName = val;
  } else {
    handled = false;
  }
}

void ApplyWindowKey(UserSettings& s, const std::string& key, const std::string& val) {
  bool handled = false;
  ApplyWindowPlacementKeys(s, key, val, handled);
  if (handled) return;

  if (key == "fullscreen") {
    s.fullscreen = IniParser::ParseBool(val);
    if (!s.windowMode.has_value()) {
      s.windowMode = s.fullscreen ? WindowMode::BorderlessFullscreen : WindowMode::Windowed;
    }
    return;
  }
  if (key == "maximized") {
    s.maximized = IniParser::ParseBool(val);
    return;
  }
  ApplyWindowModeAndDisplayKeys(s, key, val);
}

void ApplyAudioKey(UserSettings& s, const std::string& key, const std::string& val) {
  if (key == "master_volume" || key == "masterVolume") {
    s.masterVolume = std::clamp(std::stof(val), 0.0F, 1.0F);
    return;
  }
  if (key == "muted" || key == "audio_muted" || key == "audioMuted") {
    s.audioMuted = IniParser::ParseBool(val);
    return;
  }
}

void ApplyEngineKey(UserSettings& s, const std::string& key, const std::string& val) {
  if (key == "fps_target" || key == "fpsTarget") {
    s.fpsTarget = std::max(0, std::stoi(val));
    return;
  }
  if (key == "perf_overlay" || key == "perfOverlay") {
    s.showPerfOverlay = IniParser::ParseBool(val);
    return;
  }
  if (key == "vsync") {
    s.vsync = IniParser::ParseBool(val);
    return;
  }
  if (key == "presentation_mode" || key == "presentationMode") {
    if (const auto parsed = ParsePresentationMode(val)) {
      s.presentationMode = *parsed;
    }
    return;
  }
}

void ApplyPrefixedOrCustomKey(UserSettings& settings, const std::string& key, const std::string& val) {
  if (key.starts_with("window.")) {
    ApplyWindowKey(settings, key.substr(7), val);
  } else if (key.starts_with("audio.")) {
    ApplyAudioKey(settings, key.substr(6), val);
  } else if (key.starts_with("engine.")) {
    ApplyEngineKey(settings, key.substr(7), val);
  } else {
    settings.customSettings[key] = IniParser::ParseValue(val);
  }
}

void DispatchSetting(UserSettings& settings, std::string_view section, const std::string& key, const std::string& val) {
  if (section == "window") {
    ApplyWindowKey(settings, key, val);
  } else if (section == "audio") {
    ApplyAudioKey(settings, key, val);
  } else if (section == "engine") {
    ApplyEngineKey(settings, key, val);
  } else if (section == "settings" || section == "custom") {
    settings.customSettings[key] = IniParser::ParseValue(val);
  } else {
    ApplyPrefixedOrCustomKey(settings, key, val);
  }
}

#ifndef OCTARINE_PLATFORM_MOBILE
SDL_DisplayID FindTargetDisplay(const UserSettings& settings, const SDL_DisplayID* displays, int displayCount) {
  if (displays == nullptr || displayCount <= 0) {
    return SDL_GetPrimaryDisplay();
  }

  if (!settings.displayName.empty()) {
    for (int i = 0; i < displayCount; ++i) {
      const char* name = SDL_GetDisplayName(displays[i]);
      if (name != nullptr && settings.displayName == name) {
        return displays[i];
      }
    }
  }

  if (settings.displayIndex >= 0 && settings.displayIndex < displayCount) {
    return displays[settings.displayIndex];
  }

  if (settings.hasWindowPlacement) {
    const SDL_Point pt{settings.windowX, settings.windowY};
    const SDL_DisplayID ptDisp = SDL_GetDisplayForPoint(&pt);
    if (ptDisp != 0) {
      return ptDisp;
    }
  }

  const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
  return (primary != 0) ? primary : displays[0];
}

WindowRect CalculateWindowRect(const UserSettings& settings, SDL_DisplayID targetDisplay, int defaultWidth,
                               int defaultHeight) {
  SDL_Rect usableBounds{0, 0, 0, 0};
  if (!SDL_GetDisplayUsableBounds(targetDisplay, &usableBounds)) {
    SDL_GetDisplayBounds(targetDisplay, &usableBounds);
  }

  int w = (settings.windowWidth > 0) ? settings.windowWidth : defaultWidth;
  int h = (settings.windowHeight > 0) ? settings.windowHeight : defaultHeight;

  if (usableBounds.w > 0 && usableBounds.h > 0) {
    w = std::min(w, usableBounds.w);
    h = std::min(h, usableBounds.h);
  }

  int x = 0;
  int y = 0;
  if (settings.hasWindowPlacement && usableBounds.w > 0 && usableBounds.h > 0) {
    SDL_Rect displayBounds{0, 0, 0, 0};
    SDL_GetDisplayBounds(targetDisplay, &displayBounds);

    const bool withinDisplay =
        (settings.windowX >= displayBounds.x - 100 && settings.windowX < displayBounds.x + displayBounds.w &&
         settings.windowY >= displayBounds.y - 100 && settings.windowY < displayBounds.y + displayBounds.h);

    if (withinDisplay) {
      const int maxX = usableBounds.x + std::max(0, usableBounds.w - 100);
      const int maxY = usableBounds.y + std::max(0, usableBounds.h - 50);
      x = std::clamp(settings.windowX, usableBounds.x, maxX);
      y = std::clamp(settings.windowY, usableBounds.y, maxY);
    } else {
      x = usableBounds.x + (usableBounds.w - w) / 2;
      y = usableBounds.y + (usableBounds.h - h) / 2;
    }
  } else if (usableBounds.w > 0 && usableBounds.h > 0) {
    x = usableBounds.x + (usableBounds.w - w) / 2;
    y = usableBounds.y + (usableBounds.h - h) / 2;
  }

  return {x, y, w, h};
}

void CaptureDisplayInfo(SDL_Window* window, int& displayIndex, std::string& displayName) {
  const SDL_DisplayID dispID = SDL_GetDisplayForWindow(window);
  if (dispID == 0) return;

  const char* name = SDL_GetDisplayName(dispID);
  displayName = (name != nullptr) ? name : "";

  int count = 0;
  SDL_DisplayID* displays = SDL_GetDisplays(&count);
  if (displays == nullptr) return;

  for (int i = 0; i < count; ++i) {
    if (displays[i] == dispID) {
      displayIndex = i;
      break;
    }
  }
  SDL_free(displays);
}

void CaptureNormalWindowBounds(SDL_Window* window, int& x, int& y, int& w, int& h, bool& hasPlacement) {
  int curX = 0;
  int curY = 0;
  int curW = 0;
  int curH = 0;
  if (!SDL_GetWindowPosition(window, &curX, &curY) || !SDL_GetWindowSize(window, &curW, &curH)) {
    return;
  }
  if (curW > 0 && curH > 0) {
    x = curX;
    y = curY;
    w = curW;
    h = curH;
    hasPlacement = true;
  }
}

void ApplyFullscreenState(SDL_Window* window, SDL_DisplayID targetDisplay, const UserSettings& settings,
                          int defaultWidth, int defaultHeight) {
  const WindowMode mode = settings.GetEffectiveWindowMode();
  if (mode == WindowMode::ExclusiveFullscreen) {
    const int reqW = (settings.fullscreenWidth > 0)
                         ? settings.fullscreenWidth
                         : ((settings.windowWidth > 0) ? settings.windowWidth : defaultWidth);
    const int reqH = (settings.fullscreenHeight > 0)
                         ? settings.fullscreenHeight
                         : ((settings.windowHeight > 0) ? settings.windowHeight : defaultHeight);
    SDL_DisplayMode closest{};
    if (SDL_GetClosestFullscreenDisplayMode(targetDisplay, reqW, reqH, settings.fullscreenRefreshRate, true,
                                            &closest)) {
      SDL_SetWindowFullscreenMode(window, &closest);
    }
    SDL_SetWindowFullscreen(window, true);
    return;
  }
  if (mode == WindowMode::BorderlessFullscreen) {
    SDL_SetWindowFullscreenMode(window, nullptr);
    SDL_SetWindowFullscreen(window, true);
    return;
  }
  SDL_SetWindowFullscreen(window, false);
}
#endif

std::string GetSettingsFilePath(const std::string& saveDataPath) {
  if (saveDataPath.empty()) return {};
  if (saveDataPath.back() == '/' || saveDataPath.back() == '\\') {
    return saveDataPath + "user_settings.ini";
  }
  return saveDataPath + "/user_settings.ini";
}

bool ResetBuiltInFullscreenAndPresentationKey(UserSettings& s, const std::string& key) {
  if (key == "fullscreen_width" || key == "window.fullscreen_width") {
    s.fullscreenWidth = 0;
    return true;
  }
  if (key == "fullscreen_height" || key == "window.fullscreen_height") {
    s.fullscreenHeight = 0;
    return true;
  }
  if (key == "fullscreen_refresh_rate" || key == "window.fullscreen_refresh_rate") {
    s.fullscreenRefreshRate = 0.0F;
    return true;
  }
  if (key == "presentation_mode" || key == "window.presentation_mode" || key == "engine.presentation_mode") {
    s.presentationMode = s.builtInDefaults.presentationMode;
    return true;
  }
  if (key == "vsync" || key == "window.vsync" || key == "engine.vsync") {
    s.vsync = s.builtInDefaults.vsync;
    return true;
  }
  if (key == "window_resizable" || key == "window.resizable" || key == "resizable") {
    s.windowResizable = s.builtInDefaults.windowResizable;
    return true;
  }
  return false;
}

bool ResetBuiltInWindowKey(UserSettings& s, const std::string& key) {
  if (key == "fullscreen" || key == "window.fullscreen" || key == "window_mode" || key == "window.mode" ||
      key == "mode") {
    s.SetWindowMode(s.builtInDefaults.windowMode);
    return true;
  }
  if (key == "maximized" || key == "window.maximized") {
    s.maximized = false;
    return true;
  }
  if (key == "window_width" || key == "window.width") {
    s.windowWidth = s.builtInDefaults.windowWidth;
    return true;
  }
  if (key == "window_height" || key == "window.height") {
    s.windowHeight = s.builtInDefaults.windowHeight;
    return true;
  }
  return ResetBuiltInFullscreenAndPresentationKey(s, key);
}

bool ResetBuiltInAudioOrEngineKey(UserSettings& s, const std::string& key) {
  if (key == "master_volume" || key == "audio.master_volume") {
    s.masterVolume = s.builtInDefaults.masterVolume;
    return true;
  }
  if (key == "audio_muted" || key == "audio.muted" || key == "audio.audio_muted" || key == "muted") {
    s.audioMuted = s.builtInDefaults.audioMuted;
    return true;
  }
  if (key == "fps_target" || key == "engine.fps_target") {
    s.fpsTarget = s.builtInDefaults.fpsTarget;
    return true;
  }
  if (key == "perf_overlay" || key == "engine.perf_overlay") {
    s.showPerfOverlay = s.builtInDefaults.showPerfOverlay;
    return true;
  }
  return false;
}

}  // namespace

bool UserSettings::Load(const std::string& saveDataPath) {
  if (saveDataPath.empty()) return false;
  const std::string fullPath = GetSettingsFilePath(saveDataPath);
  std::ifstream file(fullPath);
  if (!file.is_open()) return false;

  std::ostringstream ss;
  ss << file.rdbuf();
  auto existingDefaults = std::move(defaultSettings);
  const auto existingBuiltIn = builtInDefaults;
  *this = Parse(ss.str());
  builtInDefaults = existingBuiltIn;
  for (auto& [key, defVal] : existingDefaults) {
    DefineSetting(key, std::move(defVal));
  }
  Logger::Info("Loaded user settings from " + fullPath);
  return true;
}

bool UserSettings::Save(const std::string& saveDataPath) const {
  if (saveDataPath.empty()) return false;
  const std::string fullPath = GetSettingsFilePath(saveDataPath);
  const bool ok = file_sandbox::WriteFileAtomic(fullPath, Serialize());
  if (ok) {
    Logger::Info("Saved user settings to " + fullPath);
  } else {
    Logger::Error("Failed to save user settings to " + fullPath);
  }
  return ok;
}

#ifndef OCTARINE_PLATFORM_MOBILE
void CaptureFullscreenModeInfo(SDL_Window* window, UserSettings& s) {
  if (!s.fullscreen) {
    s.windowMode = WindowMode::Windowed;
    return;
  }
  const SDL_DisplayMode* fsMode = SDL_GetWindowFullscreenMode(window);
  if (fsMode != nullptr) {
    s.windowMode = WindowMode::ExclusiveFullscreen;
    if (fsMode->w > 0 && fsMode->h > 0) {
      s.fullscreenWidth = fsMode->w;
      s.fullscreenHeight = fsMode->h;
    }
    if (fsMode->refresh_rate > 0.0F) {
      s.fullscreenRefreshRate = fsMode->refresh_rate;
    }
  } else if (!s.windowMode.has_value() || *s.windowMode == WindowMode::Windowed) {
    s.windowMode = WindowMode::BorderlessFullscreen;
  }
}
#endif

void UserSettings::CaptureWindow([[maybe_unused]] SDL_Window* window) {
#ifdef OCTARINE_PLATFORM_MOBILE
  // Mobile OSes manage a single fullscreen surface; desktop window placement does not apply.
  return;
#else
  if (window == nullptr) return;

  const char* driver = SDL_GetCurrentVideoDriver();
  if (driver != nullptr && std::string_view(driver) == "dummy") {
    return;
  }

  const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
  fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
  maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
  windowResizable = (flags & SDL_WINDOW_RESIZABLE) != 0;

  CaptureFullscreenModeInfo(window, *this);
  CaptureDisplayInfo(window, displayIndex, displayName);

  if (!fullscreen && !maximized) {
    CaptureNormalWindowBounds(window, windowX, windowY, windowWidth, windowHeight, hasWindowPlacement);
  }
#endif
}

void UserSettings::ApplyToWindow([[maybe_unused]] SDL_Window* window, [[maybe_unused]] int defaultWidth,
                                 [[maybe_unused]] int defaultHeight) const {
#ifdef OCTARINE_PLATFORM_MOBILE
  return;
#else
  if (window == nullptr) return;

  const char* driver = SDL_GetCurrentVideoDriver();
  if (driver != nullptr && std::string_view(driver) == "dummy") {
    return;
  }

  if (windowResizable.has_value()) {
    SDL_SetWindowResizable(window, *windowResizable);
  }

  int count = 0;
  SDL_DisplayID* displays = SDL_GetDisplays(&count);
  const SDL_DisplayID targetDisplay = FindTargetDisplay(*this, displays, count);
  if (displays != nullptr) {
    SDL_free(displays);
  }

  if (targetDisplay == 0) return;

  const WindowRect rect = CalculateWindowRect(*this, targetDisplay, defaultWidth, defaultHeight);

  SDL_SetWindowSize(window, rect.w, rect.h);
  SDL_SetWindowPosition(window, rect.x, rect.y);

  const WindowMode mode = GetEffectiveWindowMode();
  if (maximized && mode == WindowMode::Windowed) {
    SDL_MaximizeWindow(window);
  }
  ApplyFullscreenState(window, targetDisplay, *this, defaultWidth, defaultHeight);
#endif
}

void UserSettings::DefineSetting(const std::string& key, SettingValue defaultValue) {
  if (key.empty()) return;
  if (const auto it = customSettings.find(key); it != customSettings.end()) {
    if (const auto coerced = IniParser::CoerceValue(it->second, defaultValue)) {
      it->second = *coerced;
    } else {
      Logger::Warn("UserSettings::DefineSetting: saved value for '" + key +
                   "' had incompatible type; resetting to default.");
      it->second = defaultValue;
    }
  }
  defaultSettings[key] = std::move(defaultValue);
}

bool UserSettings::SetSetting(const std::string& key, SettingValue value) {
  if (key.empty()) return false;
  if (const auto defIt = defaultSettings.find(key); defIt != defaultSettings.end()) {
    const auto coerced = IniParser::CoerceValue(value, defIt->second);
    if (!coerced.has_value()) {
      Logger::Warn("UserSettings::SetSetting: type mismatch for defined setting '" + key + "'");
      return false;
    }
    customSettings[key] = *coerced;
    return true;
  }
  customSettings[key] = std::move(value);
  return true;
}

std::optional<UserSettings::SettingValue> UserSettings::GetSetting(const std::string& key) const {
  if (const auto it = customSettings.find(key); it != customSettings.end()) {
    return it->second;
  }
  if (const auto defIt = defaultSettings.find(key); defIt != defaultSettings.end()) {
    return defIt->second;
  }
  return std::nullopt;
}

std::optional<UserSettings::SettingValue> UserSettings::GetDefaultSetting(const std::string& key) const {
  if (const auto defIt = defaultSettings.find(key); defIt != defaultSettings.end()) {
    return defIt->second;
  }
  return std::nullopt;
}

bool UserSettings::HasSetting(const std::string& key) const {
  return customSettings.contains(key) || defaultSettings.contains(key);
}

bool UserSettings::ResetSetting(const std::string& key) {
  if (ResetBuiltInWindowKey(*this, key) || ResetBuiltInAudioOrEngineKey(*this, key)) {
    return true;
  }
  if (const auto defIt = defaultSettings.find(key); defIt != defaultSettings.end()) {
    customSettings[key] = defIt->second;
    return true;
  }
  return customSettings.erase(key) > 0;
}

void UserSettings::ResetAllSettings() {
  SetWindowMode(builtInDefaults.windowMode);
  maximized = false;
  windowWidth = builtInDefaults.windowWidth;
  windowHeight = builtInDefaults.windowHeight;
  fullscreenWidth = 0;
  fullscreenHeight = 0;
  fullscreenRefreshRate = 0.0F;
  presentationMode = builtInDefaults.presentationMode;
  vsync = builtInDefaults.vsync;
  windowResizable = builtInDefaults.windowResizable;
  masterVolume = builtInDefaults.masterVolume;
  audioMuted = builtInDefaults.audioMuted;
  fpsTarget = builtInDefaults.fpsTarget;
  showPerfOverlay = builtInDefaults.showPerfOverlay;

  customSettings.clear();
  for (const auto& [key, defVal] : defaultSettings) {
    customSettings[key] = defVal;
  }
}

std::string UserSettings::GetCustomSetting(const std::string& key, const std::string& defaultValue) const {
  const auto val = GetSetting(key);
  return val.has_value() ? IniParser::ValueToPlainString(*val) : defaultValue;
}

void UserSettings::SetCustomSetting(const std::string& key, const std::string& value) {
  static_cast<void>(SetSetting(key, SettingValue{value}));
}

bool UserSettings::HasCustomSetting(const std::string& key) const { return HasSetting(key); }

UserSettings UserSettings::Parse(std::string_view text) {
  UserSettings settings;
  for (const auto& entry : IniParser::ParseEntries(text)) {
    try {
      DispatchSetting(settings, entry.section, entry.key, entry.value);
    } catch (const std::exception& e) {
      Logger::Warn("UserSettings::Parse: error parsing key '" + entry.key + "': " + e.what());
    }
  }
  return settings;
}

namespace {
void SerializeWindowDisplayOptions(std::ostringstream& ss, const UserSettings& s) {
  if (s.windowMode.has_value()) {
    ss << "mode = " << WindowModeToString(*s.windowMode) << "\n";
  }
  ss << "fullscreen = " << (s.fullscreen ? "true" : "false") << "\n";
  ss << "maximized = " << (s.maximized ? "true" : "false") << "\n";
  if (s.fullscreenWidth > 0 && s.fullscreenHeight > 0) {
    ss << "fullscreen_width = " << s.fullscreenWidth << "\n";
    ss << "fullscreen_height = " << s.fullscreenHeight << "\n";
  }
  if (s.fullscreenRefreshRate > 0.0F) {
    ss << "fullscreen_refresh_rate = " << s.fullscreenRefreshRate << "\n";
  }
  if (s.presentationMode.has_value()) {
    ss << "presentation_mode = " << PresentationModeToString(*s.presentationMode) << "\n";
  }
  if (s.vsync.has_value()) {
    ss << "vsync = " << (*s.vsync ? "true" : "false") << "\n";
  }
  if (s.windowResizable.has_value()) {
    ss << "resizable = " << (*s.windowResizable ? "true" : "false") << "\n";
  }
}

void SerializeWindowSection(std::ostringstream& ss, const UserSettings& s) {
  ss << "[window]\n";
  if (s.hasWindowPlacement) {
    ss << "x = " << s.windowX << "\n";
    ss << "y = " << s.windowY << "\n";
  }
  if (s.windowWidth > 0 && s.windowHeight > 0) {
    ss << "width = " << s.windowWidth << "\n";
    ss << "height = " << s.windowHeight << "\n";
  }
  if (s.displayIndex >= 0) {
    ss << "display_index = " << s.displayIndex << "\n";
  }
  if (!s.displayName.empty()) {
    ss << "display_name = " << s.displayName << "\n";
  }
  SerializeWindowDisplayOptions(ss, s);
}

void SerializeEngineSection(std::ostringstream& ss, const UserSettings& s) {
  if (!s.fpsTarget.has_value() && !s.showPerfOverlay.has_value()) {
    return;
  }
  ss << "\n[engine]\n";
  if (s.fpsTarget.has_value()) {
    ss << "fps_target = " << *s.fpsTarget << "\n";
  }
  if (s.showPerfOverlay.has_value()) {
    ss << "perf_overlay = " << (*s.showPerfOverlay ? "true" : "false") << "\n";
  }
}
}  // namespace

std::string UserSettings::Serialize() const {
  std::ostringstream ss;
  ss << "# Octarine User Settings\n\n";

  SerializeWindowSection(ss, *this);

  ss << "\n[audio]\n";
  ss << "master_volume = " << masterVolume << "\n";
  ss << "muted = " << (audioMuted ? "true" : "false") << "\n";

  SerializeEngineSection(ss, *this);

  std::set<std::string> allKeys;
  for (const auto& [k, _] : defaultSettings) allKeys.insert(k);
  for (const auto& [k, _] : customSettings) allKeys.insert(k);

  if (!allKeys.empty()) {
    ss << "\n[settings]\n";
    for (const auto& k : allKeys) {
      if (const auto val = GetSetting(k)) {
        ss << k << " = " << IniParser::FormatValue(*val) << "\n";
      }
    }
  }

  return ss.str();
}

}  // namespace octarine
