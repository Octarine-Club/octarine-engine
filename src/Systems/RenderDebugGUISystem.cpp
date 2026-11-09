#include "Systems/RenderDebugGUISystem.h"

#ifdef OCTARINE_WITH_IMGUI
#include <algorithm>
#include <chrono>
#include <cstring>
#include <string_view>
#include <vector>

#include "Components/ViewportInfo.h"
#include "Debug/DebugComponentInspectors.h"
#include "Debug/DebugMenuRegistry.h"
#include "Debug/Panels/DebugAssetScenePanel.h"
#include "Debug/Panels/DebugEngineOptionsPanel.h"
#include "Debug/Panels/DebugEntityInspectorPanel.h"
#include "Debug/Panels/DebugLuaConsolePanel.h"
#include "Debug/Panels/DebugMainMenuBar.h"
#include "Debug/Panels/DebugSystemProfilerPanel.h"
#include "Debug/Panels/DebugUIPanel.h"
#include "Debug/Panels/DebugViewportGizmos.h"
#include "ECS/Query.h"
#include "Game/GameConfig.h"
#include "General/PerfUtils.h"
#include "Systems/TilemapDebugPanel.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "imgui_internal.h"

namespace octarine::debug {

void* SettingsReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char* name) {
  if (std::strcmp(name, "OpenState") == 0) {
    return reinterpret_cast<void*>(1);
  }
  return nullptr;
}

void SettingsReadLine(ImGuiContext*, ImGuiSettingsHandler* handler, void* entry, const char* line) {
  if (entry == nullptr || handler->UserData == nullptr) return;
  auto* reg = static_cast<Registry*>(handler->UserData);
  auto* debugReg = reg->TryGet<DebugMenuRegistry>();
  if (debugReg == nullptr) return;

  std::string_view sv(line);
  const auto eq = sv.find('=');
  if (eq != std::string_view::npos) {
    const std::string key(sv.substr(0, eq));
    const std::string_view val = sv.substr(eq + 1);
    const bool isOpen = (val == "1" || val == "true");
    debugReg->ApplyPersistedOpenState(key, isOpen);
  }
}

void SettingsWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf) {
  if (handler->UserData == nullptr) return;
  auto* reg = static_cast<Registry*>(handler->UserData);
  auto* debugReg = reg->TryGet<DebugMenuRegistry>();
  if (debugReg == nullptr) return;

  buf->appendf("[%s][OpenState]\n", handler->TypeName);
  std::string serialized = debugReg->SerializeOpenStates();
  buf->append(serialized.c_str());
  buf->append("\n");
}

void InstallImGuiSettingsHandler(Registry& registry) {
  ImGuiContext* ctx = ImGui::GetCurrentContext();
  if (ctx == nullptr) return;

  const bool alreadyInstalled = std::ranges::any_of(ctx->SettingsHandlers, [](const auto& existing) {
    return std::strcmp(existing.TypeName, "OctarineDebugPanels") == 0;
  });
  if (alreadyInstalled) return;

  ImGuiSettingsHandler iniHandler;
  iniHandler.TypeName = "OctarineDebugPanels";
  iniHandler.TypeHash = ImHashStr("OctarineDebugPanels");
  iniHandler.UserData = &registry;
  iniHandler.ReadOpenFn = SettingsReadOpen;
  iniHandler.ReadLineFn = SettingsReadLine;
  iniHandler.WriteAllFn = SettingsWriteAll;
  ctx->SettingsHandlers.push_back(iniHandler);
}

void UninstallImGuiSettingsHandler() {
  ImGuiContext* ctx = ImGui::GetCurrentContext();
  if (ctx == nullptr) return;

  for (auto& handler : ctx->SettingsHandlers) {
    if (std::strcmp(handler.TypeName, "OctarineDebugPanels") == 0) {
      handler.UserData = nullptr;
    }
  }
}

}  // namespace octarine::debug

namespace {
constexpr float kColorChannelMax = 255.0f;

ImVec4 ColorToImVec4(const octarine::Color& color) {
  return {static_cast<float>(color.r) / kColorChannelMax, static_cast<float>(color.g) / kColorChannelMax,
          static_cast<float>(color.b) / kColorChannelMax, static_cast<float>(color.a) / kColorChannelMax};
}

void InstallBuiltInDebugPanels(octarine::debug::DebugMenuRegistry& debugReg) {
  debugReg.RegisterPanel({
      .id = "engine.entity_inspector",
      .category = "ECS",
      .title = "Entity Inspector",
      .shortcut = "F1",
      .autoWindow = true,
      .drawCallback = [](Registry& reg) { octarine::debug::DrawEntityInspectorPanel(reg); },
  });

  debugReg.RegisterPanel({
      .id = "engine.lua_console",
      .category = "Engine",
      .title = "Lua Console & Logs",
      .shortcut = "~",
      .autoWindow = true,
      .drawCallback = [](Registry& reg) { octarine::debug::DrawLuaConsolePanel(reg); },
  });

  debugReg.RegisterPanel({
      .id = "engine.system_profiler",
      .category = "ECS",
      .title = "System & ECS Profiler",
      .shortcut = "",
      .autoWindow = true,
      .drawCallback = [](Registry& reg) { octarine::debug::DrawSystemProfilerPanel(reg); },
  });

  debugReg.RegisterPanel({
      .id = "engine.asset_scene_inspector",
      .category = "Assets",
      .title = "Scenes & Assets",
      .shortcut = "",
      .autoWindow = true,
      .drawCallback = [](Registry& reg) { octarine::debug::DrawAssetScenePanel(reg); },
  });

  debugReg.RegisterPanel({
      .id = "engine.ui_inspector",
      .category = "UI",
      .title = "Declarative UI Inspector",
      .shortcut = "",
      .autoWindow = true,
      .drawCallback = [](Registry& reg) { octarine::debug::DrawUIPanel(reg); },
  });

  debugReg.RegisterPanel({
      .id = "engine.options_and_watches",
      .category = "Engine",
      .title = "Engine Options & Watches",
      .shortcut = "",
      .autoWindow = true,
      .drawCallback = [](Registry& reg) { octarine::debug::DrawEngineOptionsPanel(reg); },
  });

  debugReg.RegisterPanel({
      .id = "engine.tilemap_inspector",
      .category = "World",
      .title = "Tilemap Inspector",
      .shortcut = "",
      .autoWindow = false,
      .drawCallback =
          [](Registry& reg) {
            if (auto* debugRegistry = reg.TryGet<octarine::debug::DebugMenuRegistry>()) {
              if (auto* p = debugRegistry->FindPanel("engine.tilemap_inspector")) {
                octarine::systems::DrawTilemapDebugPanel(&reg, &p->isOpen);
              }
            }
          },
  });
}

void DrawSinglePanel(Registry& registry, octarine::debug::DebugPanelDescriptor& panel) {
  if (!panel.isOpen) return;
  if (panel.autoWindow) {
    if (ImGui::Begin(panel.title.c_str(), &panel.isOpen)) {
      if (panel.drawCallback) panel.drawCallback(registry);
    }
    ImGui::End();
  } else if (panel.drawCallback) {
    panel.drawCallback(registry);
  }
}

void DrawRegisteredPanels(Registry& registry, octarine::debug::DebugMenuRegistry& debugReg) {
  for (auto& panel : debugReg.Panels()) {
    DrawSinglePanel(registry, panel);
  }
}
}  // namespace

void RenderDebugGUISystem::DrawDebugOverlays(Registry* registry, const EngineOptions& engineOptions,
                                             const bool showGameOverlays, const bool projectLoaded,
                                             const float deltaTime) {
  if (!showGameOverlays) {
    return;
  }

  auto* debugReg = registry->TryGet<octarine::debug::DebugMenuRegistry>();
  if (debugReg != nullptr && !debugReg->BuiltInsInstalled()) {
    octarine::debug::DebugComponentInspectorRegistry::InstallBuiltInComponentInspectors();
    InstallBuiltInDebugPanels(*debugReg);
    debugReg->MarkBuiltInsInstalled();
  }

  const bool hasCustomBg = (engineOptions.debugOverlayBackgroundColor.a > 0);
  if (hasCustomBg) {
    const ImVec4 bgColor = ColorToImVec4(engineOptions.debugOverlayBackgroundColor);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, bgColor);
  }

  octarine::debug::DrawDebugMainMenuBar(*registry, deltaTime);

  if (debugReg != nullptr) {
    DrawRegisteredPanels(*registry, *debugReg);
  }

  if (projectLoaded) {
    auto query = registry->CreateQuery<ScriptComponent>();
    RenderDebugGUISystem system;
    query->ForEach(system);
  }
  if (engineOptions.showEntityInfo) {
    EntityInfoWindow(registry);
  }
  if (engineOptions.showImGuiDemoWindow) {
    ImGui::ShowDemoWindow(&const_cast<EngineOptions&>(engineOptions).showImGuiDemoWindow);
  }

  octarine::debug::DrawDebugViewportGizmos(*registry);

  if (hasCustomBg) {
    ImGui::PopStyleColor();
  }
}

void RenderDebugGUISystem::Render(Registry* registry, SDL_Renderer* renderer, const float deltaTime) {
  if (!registry || !renderer) return;

  auto& gameConfig = registry->Get<GameConfig>();
  auto& engineOptions = gameConfig.GetEngineOptions();
  const bool projectLoaded = gameConfig.HasLoadedConfig();
  const bool showGameOverlays = engineOptions.showDebugGUI;
  const bool toastActive = HasActiveScriptErrorToast();

  static bool wasShowingGameOverlays = false;
  const bool justClosedOverlays = wasShowingGameOverlays && !showGameOverlays;
  wasShowingGameOverlays = showGameOverlays;

  if (justClosedOverlays) {
    ReturnFocusToGame(registry);
  }

  if (!showGameOverlays && !toastActive && !justClosedOverlays) {
    return;
  }

  PROFILE_NAMED_SCOPE("RenderDebugGUISystem::Render");

  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  if (justClosedOverlays) {
    ReturnFocusToGame(registry);
  }

  DrawDebugOverlays(registry, engineOptions, showGameOverlays, projectLoaded, deltaTime);

  if (toastActive) {
    ScriptErrorToastWindow();
  }

  ImGui::Render();
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
}

void RenderDebugGUISystem::EntityInfoWindow(const Registry* registry) {
  const auto count = registry->GetUserEntityCount();
  ImGui::Begin("Entity Info", nullptr, ImGuiWindowFlags_NoNavInputs);
  ImGui::Text("Entity Count: %llu", static_cast<unsigned long long>(count));
  ImGui::End();
}

namespace {
constexpr double kScriptErrorToastSeconds = 10.0;
constexpr float kToastBgAlpha = 0.85f;
constexpr float kToastWrapWidthPx = 420.0f;
constexpr ImVec4 kToastHeaderColor{1.0f, 0.35f, 0.35f, 1.0f};

bool IsToastFresh(const Logger::ScriptError& error) {
  const auto age =
      std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - error.when);
  return age.count() < kScriptErrorToastSeconds;
}
}  // namespace

bool RenderDebugGUISystem::HasActiveScriptErrorToast() {
  const auto errors = Logger::RecentScriptErrors();
  return std::any_of(errors.begin(), errors.end(), [](const Logger::ScriptError& e) { return IsToastFresh(e); });
}

void RenderDebugGUISystem::ScriptErrorToastWindow() {
  const auto errors = Logger::RecentScriptErrors();
  std::vector<const Logger::ScriptError*> fresh;
  for (const auto& error : errors) {
    if (IsToastFresh(error)) fresh.push_back(&error);
  }
  if (fresh.empty()) return;

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  constexpr float kMargin = 12.0f;
  ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - kMargin,
                                 viewport->WorkPos.y + viewport->WorkSize.y - kMargin),
                          ImGuiCond_Always, ImVec2(1.0f, 1.0f));
  ImGui::SetNextWindowBgAlpha(kToastBgAlpha);
  constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs |
                                      ImGuiWindowFlags_AlwaysAutoResize;
  ImGui::Begin("##script-error-toast", nullptr, kFlags);
  ImGui::TextColored(kToastHeaderColor, "Script error%s (%d)", fresh.size() == 1 ? "" : "s",
                     static_cast<int>(fresh.size()));
  ImGui::Separator();
  ImGui::PushTextWrapPos(kToastWrapWidthPx);
  for (const auto* error : fresh) {
    ImGui::TextWrapped("%s", error->message.c_str());
  }
  ImGui::PopTextWrapPos();
  ImGui::End();
}
#endif
