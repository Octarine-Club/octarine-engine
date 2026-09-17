#include "Engine/EditorBootstrap.h"

#ifdef OCTARINE_WITH_EDITOR

#include <SDL3/SDL.h>

#include "ECS/Registry.h"
#include "Editor/EditorLayoutPresets.h"
#include "Editor/EditorPersistence.h"
#include "Editor/ExportBuilder.h"
#include "Editor/HotPusher.h"
#include "Editor/Panels/EditorImGuiBackend.h"
#include "Editor/PlayerLauncher.h"
#include "Game/GameConfig.h"
#include "General/Logger.h"

namespace engine_bootstrap::editor {

void InstallSingletons(Registry& registry, std::string& effectivePath) {
  registry.Set<EditorPersistence>(EditorPersistence());
  auto& editorPersistence = registry.Get<EditorPersistence>();
  editorPersistence.LoadGlobal();
  registry.Set<octarine::editor::PlayerLauncher>(octarine::editor::PlayerLauncher());
  registry.Set<octarine::editor::ExportBuilder>(octarine::editor::ExportBuilder());
  registry.Set<octarine::editor::HotPusher>(octarine::editor::HotPusher());
  if (effectivePath.empty()) {
    effectivePath = editorPersistence.lastProjectPath;
    if (!effectivePath.empty()) {
      Logger::Info("No project path provided, attempting to load last project: " + effectivePath);
    }
  }
}

void OnProjectLoaded(Registry& registry, const std::string& path) {
  auto& editorPersistence = registry.Get<EditorPersistence>();
  editorPersistence.LoadProject(path);
  editorPersistence.lastProjectPath = path;
  editorPersistence.SaveGlobal();

  auto& options = registry.Get<GameConfig>().GetEngineOptions();
  options.showDebugGUI = editorPersistence.showDebugGUI;
  options.drawColliders = editorPersistence.drawColliders;
  options.showFpsCounter = editorPersistence.showFpsCounter;
  options.showEntityInfo = editorPersistence.showEntityInfo;
}

void ApplyAudioPrefs(Registry& registry) {
  // Editor-global audio prefs are authoritative for editor sessions, applied at startup.
  auto& editorPersistence = registry.Get<EditorPersistence>();
  auto& audioOptions = registry.Get<GameConfig>().GetEngineOptions();
  audioOptions.audioEnabled = !editorPersistence.audioMuted;
  audioOptions.masterVolume = editorPersistence.masterVolume;
}

StartupModeDecision DecideStartupMode(const std::string& startupMode) {
  return {/*ok=*/true, /*defaultToEditor=*/startupMode.empty() || startupMode == "editor"};
}

void PauseForEditorSession(Registry& registry) {
  // Open the editor with gameplay paused so the scene sits ready to inspect; the toolbar Play
  // button starts it.
  registry.Get<GameConfig>().GetEngineOptions().isPaused = true;
}

void SetupEditorImGui(Registry& registry) {
  auto& editorPersistence = registry.Get<EditorPersistence>();

  // Resolve editor font size (DPI-aware default on first launch). Roboto renders a bit smaller than
  // ProggyClean at the same px, so the unscaled default is 17 rather than the 16px baseline.
  constexpr float kUnscaledDefaultFontPx = 17.0F;
  float fontSize = editorPersistence.editorFontSize;
  if (fontSize <= 0.0F) {
    const SDL_DisplayID displayId = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(displayId);
    float dpiScale = 1.0F;
    if (mode != nullptr && mode->pixel_density > 0.0F) {
      dpiScale = mode->pixel_density;
    }
    fontSize = kUnscaledDefaultFontPx * dpiScale;
    editorPersistence.editorFontSize = fontSize;
  }
  octarine::editor::panels::RebuildEditorFont(fontSize);
  octarine::editor::panels::ApplyEditorStyle(editorPersistence.editorStyleIndex, fontSize);

  // First-run: if ImGui has no saved layout for this project / pref dir, apply the bundled default
  // so the user sees a curated workspace instead of a pile of floating windows.
  if (!octarine::editor::layouts::HasImGuiIni()) {
    octarine::editor::layouts::ApplyDefaultPreset(editorPersistence);
  }
}

void SaveOnShutdown(Registry& registry) {
  auto& gameConfig = registry.Get<GameConfig>();
  if (auto* editorPersistence = registry.TryGet<EditorPersistence>()) {
    const auto& options = gameConfig.GetEngineOptions();
    editorPersistence->audioMuted = !options.audioEnabled;
    editorPersistence->masterVolume = options.masterVolume;
    editorPersistence->showDebugGUI = options.showDebugGUI;
    editorPersistence->drawColliders = options.drawColliders;
    editorPersistence->showFpsCounter = options.showFpsCounter;
    editorPersistence->showEntityInfo = options.showEntityInfo;
    editorPersistence->SaveGlobal();
    if (gameConfig.HasLoadedConfig()) {
      editorPersistence->SaveProject(gameConfig.GetAssetPath());
    }
  }
}

void DisableBenchOverlays(Registry& registry) {
  auto& bench_editor = registry.Get<EditorPersistence>();
  bench_editor.showProfiler = false;
  bench_editor.showHierarchy = false;
  bench_editor.showAssetBrowser = false;
  bench_editor.showSceneWindow = false;
  bench_editor.showLuaConsole = false;
  bench_editor.showSceneManagement = false;
}

}  // namespace engine_bootstrap::editor

#else  // OCTARINE_WITH_EDITOR — player build: dev-player persistence (non-shipped) or no-op stubs (shipped).

#include "General/Logger.h"
#ifdef OCTARINE_WITH_IMGUI
#include "imgui.h"
#endif

#ifndef OCTARINE_SHIPPED
#include "ECS/Registry.h"
#include "Editor/EditorPersistence.h"
#include "Game/GameConfig.h"
#endif

namespace engine_bootstrap::editor {

#ifndef OCTARINE_SHIPPED
void InstallSingletons(Registry& registry, std::string& /*effectivePath*/) {
  registry.Set<EditorPersistence>(EditorPersistence());
}

void OnProjectLoaded(Registry& registry, const std::string& path) {
  if (auto* editorPersistence = registry.TryGet<EditorPersistence>()) {
    editorPersistence->LoadProject(path);
    auto& options = registry.Get<GameConfig>().GetEngineOptions();
    options.showDebugGUI = editorPersistence->showDebugGUI;
    options.drawColliders = editorPersistence->drawColliders;
    options.showFpsCounter = editorPersistence->showFpsCounter;
    options.showEntityInfo = editorPersistence->showEntityInfo;
  }
}

void ApplyAudioPrefs(Registry& /*registry*/) {}

void SaveOnShutdown(Registry& registry) {
  auto& gameConfig = registry.Get<GameConfig>();
  if (auto* editorPersistence = registry.TryGet<EditorPersistence>()) {
    const auto& options = gameConfig.GetEngineOptions();
    editorPersistence->showDebugGUI = options.showDebugGUI;
    editorPersistence->drawColliders = options.drawColliders;
    editorPersistence->showFpsCounter = options.showFpsCounter;
    editorPersistence->showEntityInfo = options.showEntityInfo;
    if (gameConfig.HasLoadedConfig()) {
      editorPersistence->SaveProject(gameConfig.GetAssetPath());
    }
  }
}
#else   // OCTARINE_SHIPPED — shipping build: no-ops, never touches editor_prefs.ini
void InstallSingletons(Registry& /*registry*/, std::string& /*effectivePath*/) {}
void OnProjectLoaded(Registry& /*registry*/, const std::string& /*path*/) {}
void ApplyAudioPrefs(Registry& /*registry*/) {}
void SaveOnShutdown(Registry& /*registry*/) {}
#endif  // OCTARINE_SHIPPED

StartupModeDecision DecideStartupMode(const std::string& startupMode) {
  if (startupMode == "editor") {
    Logger::Error(
        "--startup-mode editor was requested, but the editor is experimental and disabled in this build. "
        "Recompile with -DOCTARINE_WITH_EDITOR=ON to enable.");
    return {/*ok=*/false, /*defaultToEditor=*/false};
  }
  return {/*ok=*/true, /*defaultToEditor=*/false};
}

void PauseForEditorSession(Registry& /*registry*/) {}

void SetupEditorImGui(Registry& /*registry*/) {
#ifdef OCTARINE_WITH_IMGUI
  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->Clear();
  io.Fonts->AddFontDefault();
  io.Fonts->Build();
#endif
}

void DisableBenchOverlays(Registry& /*registry*/) {}

}  // namespace engine_bootstrap::editor

#endif  // OCTARINE_WITH_EDITOR
