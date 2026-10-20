#pragma once

#include <SDL3/SDL.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__ANDROID__) || defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS) || \
    (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
#ifndef OCTARINE_PLATFORM_MOBILE
#define OCTARINE_PLATFORM_MOBILE 1
#endif
#endif

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace octarine {

// Persisted user preferences and runtime configuration.
// Stored in the per-user save-data path (<SaveDataPath>/user_settings.ini).
// Automatically records and restores desktop window placement across multi-monitor setups,
// audio volume/mute, engine overrides, and typed game settings defined in C++ or Lua.
struct UserSettings {
  using SettingValue = std::variant<bool, int64_t, double, std::string>;

  // Window & Screen Placement (desktop only; no-op on mobile platforms)
  int windowX = -1;
  int windowY = -1;
  int windowWidth = 0;
  int windowHeight = 0;
  int displayIndex = -1;
  std::string displayName{};
  bool fullscreen = false;
  bool maximized = false;
  bool hasWindowPlacement = false;

  // Audio configuration
  float masterVolume = 1.0F;
  bool audioMuted = false;

  // Engine configuration overrides
  std::optional<int> fpsTarget{};
  std::optional<bool> showPerfOverlay{};

  // Typed user/game settings persisted under [settings]
  std::unordered_map<std::string, SettingValue> customSettings{};

  // Registered default values and schema types defined by C++ or Lua
  std::unordered_map<std::string, SettingValue> defaultSettings{};

  // Loads settings from `<saveDataPath>/user_settings.ini`. Returns true if loaded successfully.
  [[nodiscard]] bool Load(const std::string& saveDataPath);

  // Saves settings to `<saveDataPath>/user_settings.ini` atomically. Returns true on success.
  [[nodiscard]] bool Save(const std::string& saveDataPath) const;

  // Captures the live window state (flags, display, position, and dimensions) from an SDL window.
  // No-op on mobile platforms (Android/iOS) or dummy video drivers.
  void CaptureWindow(SDL_Window* window);

  // Restores saved window position, dimensions, display screen, maximized and fullscreen states.
  // No-op on mobile platforms (Android/iOS) or dummy video drivers.
  void ApplyToWindow(SDL_Window* window, int defaultWidth, int defaultHeight) const;

  // Defines a setting key with its default value and expected type.
  // If a value for `key` was already loaded from disk, coerces it to match `defaultValue`'s type
  // or resets it to `defaultValue` if incompatible.
  void DefineSetting(const std::string& key, SettingValue defaultValue);

  // Sets a typed setting value. If `key` was defined with a default, validates type compatibility.
  bool SetSetting(const std::string& key, SettingValue value);

  // Gets the current typed setting value (persisted override if present, otherwise defined default).
  [[nodiscard]] std::optional<SettingValue> GetSetting(const std::string& key) const;

  // Gets the registered default value for `key`, if defined.
  [[nodiscard]] std::optional<SettingValue> GetDefaultSetting(const std::string& key) const;

  // Checks whether a setting key is defined or has a custom value.
  [[nodiscard]] bool HasSetting(const std::string& key) const;

  // Resets a setting key to its defined default (or removes it if no default was defined).
  bool ResetSetting(const std::string& key);

  // Resets all custom settings back to their defined defaults.
  void ResetAllSettings();

  // Gets a custom setting formatted as a string, returning defaultValue if not found.
  [[nodiscard]] std::string GetCustomSetting(const std::string& key, const std::string& defaultValue = "") const;

  // Sets a custom string setting.
  void SetCustomSetting(const std::string& key, const std::string& value);

  // Checks whether a setting key exists.
  [[nodiscard]] bool HasCustomSetting(const std::string& key) const;

  // Parses INI formatted text into a UserSettings struct.
  static UserSettings Parse(std::string_view text);

  // Serializes UserSettings into an INI formatted string.
  [[nodiscard]] std::string Serialize() const;
};

}  // namespace octarine
