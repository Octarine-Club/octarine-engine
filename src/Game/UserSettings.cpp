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

void ApplyWindowKey(UserSettings& s, const std::string& key, const std::string& val) {
  if (key == "x" || key == "y") {
    (key == "x" ? s.windowX : s.windowY) = std::stoi(val);
    s.hasWindowPlacement = true;
    return;
  }
  if (key == "width") {
    s.windowWidth = std::stoi(val);
    return;
  }
  if (key == "height") {
    s.windowHeight = std::stoi(val);
    return;
  }
  if (key == "display_index" || key == "displayIndex") {
    s.displayIndex = std::stoi(val);
    return;
  }
  if (key == "display_name" || key == "displayName") {
    s.displayName = val;
    return;
  }
  if (key == "fullscreen" || key == "maximized") {
    (key == "fullscreen" ? s.fullscreen : s.maximized) = IniParser::ParseBool(val);
    return;
  }
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
#endif

std::string GetSettingsFilePath(const std::string& saveDataPath) {
  if (saveDataPath.empty()) return {};
  if (saveDataPath.back() == '/' || saveDataPath.back() == '\\') {
    return saveDataPath + "user_settings.ini";
  }
  return saveDataPath + "/user_settings.ini";
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
  *this = Parse(ss.str());
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

  if (maximized && !fullscreen) {
    SDL_MaximizeWindow(window);
  }
  if (fullscreen) {
    SDL_SetWindowFullscreen(window, true);
  }
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
  if (const auto defIt = defaultSettings.find(key); defIt != defaultSettings.end()) {
    customSettings[key] = defIt->second;
    return true;
  }
  return customSettings.erase(key) > 0;
}

void UserSettings::ResetAllSettings() {
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

std::string UserSettings::Serialize() const {
  std::ostringstream ss;
  ss << "# Octarine User Settings\n\n";

  ss << "[window]\n";
  if (hasWindowPlacement) {
    ss << "x = " << windowX << "\n";
    ss << "y = " << windowY << "\n";
  }
  if (windowWidth > 0 && windowHeight > 0) {
    ss << "width = " << windowWidth << "\n";
    ss << "height = " << windowHeight << "\n";
  }
  if (displayIndex >= 0) {
    ss << "display_index = " << displayIndex << "\n";
  }
  if (!displayName.empty()) {
    ss << "display_name = " << displayName << "\n";
  }
  ss << "fullscreen = " << (fullscreen ? "true" : "false") << "\n";
  ss << "maximized = " << (maximized ? "true" : "false") << "\n";

  ss << "\n[audio]\n";
  ss << "master_volume = " << masterVolume << "\n";
  ss << "muted = " << (audioMuted ? "true" : "false") << "\n";

  if (fpsTarget.has_value() || showPerfOverlay.has_value()) {
    ss << "\n[engine]\n";
    if (fpsTarget.has_value()) {
      ss << "fps_target = " << *fpsTarget << "\n";
    }
    if (showPerfOverlay.has_value()) {
      ss << "perf_overlay = " << (*showPerfOverlay ? "true" : "false") << "\n";
    }
  }

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
