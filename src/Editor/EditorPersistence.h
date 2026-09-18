#pragma once

#ifndef OCTARINE_SHIPPED

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__ANDROID__) || defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS) || \
    (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
#define OCTARINE_PLATFORM_MOBILE 1
#endif

#include <array>
#include <string>
#include <utility>

// Persistent state for development sessions (editor and dev player). Compiled out of shipped builds
// (OCTARINE_SHIPPED) so shipping releases never read or write local project preference files.
//
// Two stores:
//   * Global  — SDL pref dir / "editor_settings.ini": which project to reopen,
//               font size, theme. Survives across projects.
//   * Project — "<project>/editor_prefs.ini": per-project window layout and debug toggles.
//               Travels with the project during development.
struct EditorPersistence {
  // Global
  std::string lastProjectPath;
  float editorFontSize = 0.0F;
  int editorStyleIndex = 0;
  // Audio mute + master volume. Persisted globally so they survive across projects (and with no
  // project loaded). Applied to the live EngineOptions at startup; EngineOptions stays the source
  // AudioSystem reads, these are its persisted shadow.
  bool audioMuted = false;
  float masterVolume = 1.0F;

  // Transient: set when the editor wants the font atlas rebuilt outside the
  // active ImGui frame. Not serialized.
  bool fontRebuildPending = false;

  // Transient: set when a layout preset should be loaded at the start of the next frame.
  // Contains the preset name to load.
  std::string pendingLayoutLoad;

  // Per-project
  std::string currentScenePath;
  bool showProfiler = false;
  bool showHierarchy = false;
  bool showAssetBrowser = false;
  bool showLuaConsole = false;
  bool showSceneWindow = true;
  bool showSceneManagement = false;
  bool showEngineOptions = true;
  bool showEditorSettings = true;
  bool showPlayerOutput = false;
  bool showExportOutput = false;
  bool showSigningSettings = false;
  bool showDevices = false;
  bool showDebugGUI = false;
  bool drawColliders = false;
  bool showFpsCounter = true;
  bool showEntityInfo = false;

  // Single source of truth for persisted window-visibility flags. Both the project-prefs
  // serializer (EditorPersistence.cpp) and the layout-preset serializer (EditorLayoutPresets.cpp)
  // iterate this table, so a new window = one entry here.
  using FlagRef = std::pair<const char*, bool EditorPersistence::*>;
  static constexpr std::array<FlagRef, 12> kWindowFlags = {{
      {"showProfiler", &EditorPersistence::showProfiler},
      {"showHierarchy", &EditorPersistence::showHierarchy},
      {"showAssetBrowser", &EditorPersistence::showAssetBrowser},
      {"showLuaConsole", &EditorPersistence::showLuaConsole},
      {"showSceneWindow", &EditorPersistence::showSceneWindow},
      {"showSceneManagement", &EditorPersistence::showSceneManagement},
      {"showEngineOptions", &EditorPersistence::showEngineOptions},
      {"showEditorSettings", &EditorPersistence::showEditorSettings},
      {"showPlayerOutput", &EditorPersistence::showPlayerOutput},
      {"showExportOutput", &EditorPersistence::showExportOutput},
      {"showSigningSettings", &EditorPersistence::showSigningSettings},
      {"showDevices", &EditorPersistence::showDevices},
  }};

  void LoadGlobal();
  void SaveGlobal() const;

  void LoadProject(const std::string& projectAssetPath);
  void SaveProject(const std::string& projectAssetPath) const;
};

// Splits "key=value" into key/value, trimming trailing whitespace and CR (handles CRLF files /
// hand edits). Returns false when the line has no '='.
bool ParseIniLine(const std::string& line, std::string& key, std::string& value);

#endif  // OCTARINE_SHIPPED
