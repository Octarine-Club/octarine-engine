#include "Debug/Panels/DebugMainMenuBar.h"

#ifdef OCTARINE_WITH_IMGUI
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "Debug/DebugMenuRegistry.h"
#include "ECS/Registry.h"
#include "ECS/StateInspector.h"
#include "Engine/SceneLoader.h"
#include "Game/GameConfig.h"
#include "imgui.h"

namespace octarine::debug {

namespace {

void DrawCategoryPanelsAndActions(Registry& registry, DebugMenuRegistry& debugReg, std::string_view category) {
  for (auto& panel : debugReg.Panels()) {
    if (panel.category == category) {
      ImGui::MenuItem(panel.title.c_str(), panel.shortcut.empty() ? nullptr : panel.shortcut.c_str(), &panel.isOpen);
    }
  }

  for (const auto& toggle : debugReg.Toggles()) {
    if (toggle.category == category) {
      bool state = toggle.getter();
      if (ImGui::MenuItem(toggle.label.c_str(), nullptr, &state)) {
        toggle.setter(state);
      }
    }
  }

  for (const auto& action : debugReg.Actions()) {
    if (action.category == category) {
      if (ImGui::MenuItem(action.label.c_str(), action.shortcut.empty() ? nullptr : action.shortcut.c_str())) {
        action.callback(registry);
      }
    }
  }
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
    ImGui::MenuItem("Built-in Perf Overlay", nullptr, &opts.showPerfOverlay);

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

void DrawTransportControls(Registry& registry, EngineOptions& opts, float deltaTime) {
  constexpr float kRightPadding = 420.0f;
  const float avail = ImGui::GetContentRegionAvail().x;
  if (avail > kRightPadding) {
    ImGui::SameLine(ImGui::GetWindowWidth() - kRightPadding);
  }

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
  ImGui::SetNextItemWidth(70.0f);
  ImGui::DragFloat("##speed", &opts.timeScale, 0.05f, 0.05f, 8.0f, "%.2fx");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Simulation time scale (Right-click to reset)");
  if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) opts.timeScale = 1.0f;

  ImGui::SameLine();
  const float fps = deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f;
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

  ImGui::Text("%.0f FPS | Ent: %llu | Hash: ", static_cast<double>(fps),
              static_cast<unsigned long long>(registry.GetUserEntityCount()));
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
