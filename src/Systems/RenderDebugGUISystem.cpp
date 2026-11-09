#include "Systems/RenderDebugGUISystem.h"

#ifdef OCTARINE_WITH_IMGUI
#include <algorithm>
#include <chrono>
#include <vector>

#include "Components/ViewportInfo.h"
#include "ECS/Query.h"
#include "Game/GameConfig.h"
#include "General/PerfUtils.h"
#include "Systems/TilemapDebugPanel.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "imgui_internal.h"

void RenderDebugGUISystem::ReturnFocusToGame(Registry* registry) {
  if (!registry) return;
  auto& viewportInfo = registry->Get<ViewportInfo>();

  if (ImGui::GetCurrentContext()) {
    ImGuiIO& io = ImGui::GetIO();
    io.ClearInputKeys();
    io.ClearEventsQueue();
    ImGui::ClearActiveID();

    ImGui::SetWindowFocus(nullptr);
    viewportInfo.isFocused = true;
    io.WantCaptureKeyboard = false;
    io.NavActive = false;
  } else {
    viewportInfo.isFocused = true;
  }
}

namespace {
constexpr float kColorChannelMax = 255.0f;

ImVec4 ColorToImVec4(const octarine::Color& color) {
  return {static_cast<float>(color.r) / kColorChannelMax, static_cast<float>(color.g) / kColorChannelMax,
          static_cast<float>(color.b) / kColorChannelMax, static_cast<float>(color.a) / kColorChannelMax};
}
}  // namespace

void RenderDebugGUISystem::DrawDebugOverlays(Registry* registry, const EngineOptions& engineOptions,
                                             const bool showGameOverlays, const bool projectLoaded,
                                             const float deltaTime) {
  if (!showGameOverlays && !engineOptions.showFpsCounter) {
    return;
  }
  const ImVec4 bgColor = ColorToImVec4(engineOptions.debugOverlayBackgroundColor);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, bgColor);

  if (showGameOverlays) {
    if (projectLoaded) {
      auto query = registry->CreateQuery<ScriptComponent>();
      RenderDebugGUISystem system;
      query->ForEach(system);
    }
    if (engineOptions.showEntityInfo) {
      EntityInfoWindow(registry);
    }
    octarine::systems::DrawTilemapDebugPanel(registry);
    FPSWindow(deltaTime);
  } else if (engineOptions.showFpsCounter) {
    FPSWindow(deltaTime);
  }

  ImGui::PopStyleColor();
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

  if (!showGameOverlays && !engineOptions.showFpsCounter && !toastActive && !justClosedOverlays) {
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

void RenderDebugGUISystem::FPSWindow(const float deltaTime) {
  static constexpr int kFpsHistoryCount = 120;
  static constexpr float kFpsPlotScaleMax = 120.0f;
  static constexpr float kFpsPlotHeight = 50.0f;

  static float fpsHistory[kFpsHistoryCount] = {};
  static int fpsOffset = 0;
  const float fps = (deltaTime > 0.0f) ? 1.0f / deltaTime : 0.0f;
  fpsHistory[fpsOffset] = fps;
  fpsOffset = (fpsOffset + 1) % kFpsHistoryCount;

  ImGui::Begin("FPS", nullptr, ImGuiWindowFlags_NoNavInputs);
  char overlay[32];
  snprintf(overlay, sizeof(overlay), "%.1f FPS", static_cast<double>(fps));
  ImGui::PlotLines("##fps", fpsHistory, kFpsHistoryCount, fpsOffset, overlay, 0.0f, kFpsPlotScaleMax,
                   ImVec2(0, kFpsPlotHeight));
  ImGui::End();
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
