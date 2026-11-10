#include "Debug/Panels/DebugMainMenuBar.h"

#ifdef OCTARINE_WITH_IMGUI
#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "Debug/DebugMenuRegistry.h"
#include "ECS/Registry.h"
#include "ECS/StateInspector.h"
#include "Engine/SceneLoader.h"
#include "Game/EngineContext.h"
#include "Game/GameConfig.h"
#include "General/PerformanceTracker.h"
#include "imgui.h"

namespace octarine::debug {

namespace {

void DrawCategoryPanels(DebugMenuRegistry& debugReg, std::string_view category) {
  for (auto& panel : debugReg.Panels()) {
    if (panel.category == category) {
      ImGui::MenuItem(panel.title.c_str(), panel.shortcut.empty() ? nullptr : panel.shortcut.c_str(), &panel.isOpen);
    }
  }
}

void DrawCategoryToggles(DebugMenuRegistry& debugReg, std::string_view category) {
  for (const auto& toggle : debugReg.Toggles()) {
    if (toggle.category == category) {
      bool state = toggle.getter();
      if (ImGui::MenuItem(toggle.label.c_str(), nullptr, &state)) {
        toggle.setter(state);
      }
    }
  }
}

void DrawCategoryActions(Registry& registry, DebugMenuRegistry& debugReg, std::string_view category) {
  for (const auto& action : debugReg.Actions()) {
    if (action.category == category) {
      if (ImGui::MenuItem(action.label.c_str(), action.shortcut.empty() ? nullptr : action.shortcut.c_str())) {
        action.callback(registry);
      }
    }
  }
}

void DrawCategoryPanelsAndActions(Registry& registry, DebugMenuRegistry& debugReg, std::string_view category) {
  DrawCategoryPanels(debugReg, category);
  DrawCategoryToggles(debugReg, category);
  DrawCategoryActions(registry, debugReg, category);
}

void DrawEngineMenu(Registry& registry, DebugMenuRegistry& debugReg) {
  if (ImGui::BeginMenu("Engine")) {
    DrawCategoryPanelsAndActions(registry, debugReg, "Engine");
    ImGui::Separator();
    if (ImGui::MenuItem("Reload Current Scene", "Ctrl+R")) {
      if (auto* loader = registry.TryGet<SceneLoader*>()) {
        if (*loader) (*loader)->ReloadScene();
      }
    }
    if (ImGui::MenuItem("Close All Panels")) {
      debugReg.CloseAllPanels();
    }
    ImGui::EndMenu();
  }
}

void DrawEcsMenu(Registry& registry, DebugMenuRegistry& debugReg) {
  if (ImGui::BeginMenu("ECS")) {
    DrawCategoryPanelsAndActions(registry, debugReg, "ECS");
    ImGui::EndMenu();
  }
}

void DrawWorldMenu(Registry& registry, DebugMenuRegistry& debugReg, EngineOptions& opts) {
  if (ImGui::BeginMenu("World")) {
    DrawCategoryPanelsAndActions(registry, debugReg, "World");
    ImGui::Separator();
    ImGui::MenuItem("Draw Colliders", nullptr, &opts.drawColliders);

    bool pick = debugReg.IsWorldPickerEnabled();
    if (ImGui::MenuItem("Pick Entity in Viewport", "Alt+Click", &pick)) {
      debugReg.SetWorldPickerEnabled(pick);
    }
    bool highlight = debugReg.HighlightSelectedEntity();
    if (ImGui::MenuItem("Highlight Selected Entity", nullptr, &highlight)) {
      debugReg.SetHighlightSelectedEntity(highlight);
    }
    ImGui::EndMenu();
  }
}

void DrawAssetsMenu(Registry& registry, DebugMenuRegistry& debugReg) {
  if (ImGui::BeginMenu("Assets")) {
    DrawCategoryPanelsAndActions(registry, debugReg, "Assets");
    ImGui::EndMenu();
  }
}

void DrawUIMenu(Registry& registry, DebugMenuRegistry& debugReg) {
  if (ImGui::BeginMenu("UI")) {
    DrawCategoryPanelsAndActions(registry, debugReg, "UI");
    ImGui::Separator();
    bool layout = debugReg.ShowUILayoutBoxes();
    if (ImGui::MenuItem("Show Layout Boxes", nullptr, &layout)) {
      debugReg.SetShowUILayoutBoxes(layout);
    }
    bool clip = debugReg.ShowUIClipRects();
    if (ImGui::MenuItem("Show Clip Scissor Rects", nullptr, &clip)) {
      debugReg.SetShowUIClipRects(clip);
    }
    bool focus = debugReg.HighlightUIFocus();
    if (ImGui::MenuItem("Highlight Focused Element", nullptr, &focus)) {
      debugReg.SetHighlightUIFocus(focus);
    }
    ImGui::EndMenu();
  }
}

void DrawGameMenu(Registry& registry, DebugMenuRegistry& debugReg) {
  if (ImGui::BeginMenu("Game")) {
    DrawCategoryPanelsAndActions(registry, debugReg, "Game");
    ImGui::EndMenu();
  }
}

void DrawPlaybackButtons(EngineOptions& opts) {
  if (ImGui::Button(opts.isPaused ? " Play " : " Pause ")) {
    opts.isPaused = !opts.isPaused;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle simulation pause");

  ImGui::SameLine();
  if (ImGui::Button("Step")) {
    opts.isPaused = true;
    opts.stepFrame = true;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Advance simulation by 1 step");

  ImGui::SameLine();
  ImGui::SetNextItemWidth(65.0f);
  ImGui::DragFloat("##speed", &opts.timeScale, 0.05f, 0.05f, 8.0f, "%.2fx");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Simulation time scale (Right-click to reset)");
  if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) opts.timeScale = 1.0f;
}

struct StabilizedFpsState {
  float timer = 0.0f;
  float displayFps = 0.0f;
  float displayFrameMs = 0.0f;
  int frameCount = 0;
  float accumulatedTime = 0.0f;
};

StabilizedFpsState& GetFpsState() {
  static StabilizedFpsState state;
  return state;
}

void UpdateStabilizedFps(Registry& registry, float deltaTime) {
  auto& s = GetFpsState();
  s.frameCount++;
  s.accumulatedTime += deltaTime;
  s.timer -= deltaTime;

  if (s.timer <= 0.0f) {
    s.timer = 0.25f;  // Refresh 4 times per second
    const auto* ctx = registry.TryGet<EngineContext>();
    if (ctx && ctx->perfStats && ctx->perfStats->avgFps > 0.0f) {
      s.displayFps = ctx->perfStats->avgFps;
      s.displayFrameMs = ctx->perfStats->avgFrameTimeMs;
    } else if (s.accumulatedTime > 0.0f) {
      s.displayFps = static_cast<float>(s.frameCount) / s.accumulatedTime;
      s.displayFrameMs = (s.accumulatedTime / static_cast<float>(s.frameCount)) * 1000.0f;
    }
    s.frameCount = 0;
    s.accumulatedTime = 0.0f;
  }
}

void DrawStateHashSection(Registry& registry, EngineOptions& opts, bool compact) {
  auto& s = GetFpsState();
  static uint64_t cachedHash = 0;
  static bool hasCachedHash = false;
  if (opts.isPaused) {
    if (!hasCachedHash || opts.stepFrame) {
      cachedHash = octarine::inspect::StateInspector::ComputeStateHash(registry);
      hasCachedHash = true;
    }
  } else {
    hasCachedHash = false;
  }

  char hashLabel[32];
  if (hasCachedHash) {
    std::snprintf(hashLabel, sizeof(hashLabel), "0x%08llX", static_cast<unsigned long long>(cachedHash));
  } else {
    std::snprintf(hashLabel, sizeof(hashLabel), "<hash>");
  }

  const auto userEntCount = static_cast<unsigned long long>(registry.GetUserEntityCount());
  if (compact) {
    ImGui::Text("%.0f FPS | %llu |", static_cast<double>(s.displayFps), userEntCount);
  } else {
    ImGui::Text("%.0f FPS | Ent: %llu | Hash:", static_cast<double>(s.displayFps), userEntCount);
  }

  if (ImGui::IsItemHovered()) {
    const auto* ctx = registry.TryGet<EngineContext>();
    if (ctx && ctx->perfStats) {
      ImGui::BeginTooltip();
      ImGui::Text("FPS: %.1f (Avg: %.1f | P50: %.1f | P95: %.1f | P99: %.1f)", static_cast<double>(ctx->perfStats->fps),
                  static_cast<double>(ctx->perfStats->avgFps), static_cast<double>(ctx->perfStats->p50Fps),
                  static_cast<double>(ctx->perfStats->p95Fps), static_cast<double>(ctx->perfStats->p99Fps));
      ImGui::Text("Frame Time: %.2f ms (Avg: %.2f ms | P95: %.2f ms)", static_cast<double>(ctx->perfStats->frameTimeMs),
                  static_cast<double>(ctx->perfStats->avgFrameTimeMs),
                  static_cast<double>(ctx->perfStats->p95FrameTimeMs));
      ImGui::Text("Memory: %.2f MB", ctx->perfStats->memoryMb);
      ImGui::EndTooltip();
    }
  }

  ImGui::SameLine();
  if (ImGui::SmallButton(hashLabel)) {
    cachedHash = octarine::inspect::StateInspector::ComputeStateHash(registry);
    hasCachedHash = true;
    char clipBuf[32];
    std::snprintf(clipBuf, sizeof(clipBuf), "0x%08llX", static_cast<unsigned long long>(cachedHash));
    ImGui::SetClipboardText(clipBuf);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Canonical state hash (auto-computed while paused, click to recompute/copy)");
  }
}

void DrawTransportControls(Registry& registry, EngineOptions& opts, float deltaTime) {
  UpdateStabilizedFps(registry, deltaTime);

  const auto& style = ImGui::GetStyle();
  const float spacing = style.ItemSpacing.x;
  const float framePad = style.FramePadding.x;

  // Approximate control widths
  // Play/Pause button (~50px), Step button (~40px), Speed drag (65px)
  const float playbackWidth = 50.0f + spacing + 40.0f + spacing + 65.0f;

  // Measure text and button
  auto& s = GetFpsState();
  const auto userEntCount = static_cast<unsigned long long>(registry.GetUserEntityCount());

  char fullText[64];
  std::snprintf(fullText, sizeof(fullText), "%.0f FPS | Ent: %llu | Hash:", static_cast<double>(s.displayFps),
                userEntCount);
  const float fullTextWidth = ImGui::CalcTextSize(fullText).x;

  char compactText[64];
  std::snprintf(compactText, sizeof(compactText), "%.0f FPS | %llu |", static_cast<double>(s.displayFps), userEntCount);
  const float compactTextWidth = ImGui::CalcTextSize(compactText).x;

  // Hash button is max 10 chars ("0x12345678") + padding
  const float hashButtonWidth = ImGui::CalcTextSize("0x12345678").x + (framePad * 2.0f) + 4.0f;

  const float fullTotalWidth = playbackWidth + spacing + fullTextWidth + spacing + hashButtonWidth;
  const float compactTotalWidth = playbackWidth + spacing + compactTextWidth + spacing + hashButtonWidth;

  const float windowWidth = ImGui::GetWindowWidth();
  const float currentX = ImGui::GetCursorPosX();
  const float rightMargin = style.WindowPadding.x + 8.0f;

  bool compact = false;
  float chosenWidth = fullTotalWidth;

  if (currentX + fullTotalWidth + rightMargin > windowWidth) {
    compact = true;
    chosenWidth = compactTotalWidth;
  }

  const float targetX = std::max(currentX + spacing, windowWidth - chosenWidth - rightMargin);
  ImGui::SameLine(targetX);

  DrawPlaybackButtons(opts);
  ImGui::SameLine();
  DrawStateHashSection(registry, opts, compact);
}

}  // namespace

void DrawDebugMainMenuBar(Registry& registry, float deltaTime) {
  auto* debugReg = registry.TryGet<DebugMenuRegistry>();
  if (debugReg == nullptr) return;

  auto& gameConfig = registry.Get<GameConfig>();
  auto& opts = gameConfig.GetEngineOptions();

  if (ImGui::BeginMainMenuBar()) {
    DrawEngineMenu(registry, *debugReg);
    DrawEcsMenu(registry, *debugReg);
    DrawWorldMenu(registry, *debugReg, opts);
    DrawAssetsMenu(registry, *debugReg);
    DrawUIMenu(registry, *debugReg);
    DrawGameMenu(registry, *debugReg);

    DrawTransportControls(registry, opts, deltaTime);

    ImGui::EndMainMenuBar();
  }
}

}  // namespace octarine::debug

#endif  // OCTARINE_WITH_IMGUI
