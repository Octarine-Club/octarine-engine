#include "Debug/Panels/DebugEngineOptionsPanel.h"

#ifdef OCTARINE_WITH_IMGUI
#include "Debug/DebugMenuRegistry.h"
#include "ECS/Registry.h"
#include "Game/GameConfig.h"
#include "General/ThreadPool.h"
#include "imgui.h"

namespace octarine::debug {

namespace {

void DrawWatchesTable(const std::vector<DebugWatchDescriptor>& watches) {
  if (!ImGui::BeginTable("##watches_table", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) return;
  ImGui::TableSetupColumn("Watch Label", ImGuiTableColumnFlags_WidthFixed, 180.0f);
  ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableHeadersRow();

  for (const auto& w : watches) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(w.label.c_str());

    ImGui::TableSetColumnIndex(1);
    const std::string val = w.valueFormatter ? w.valueFormatter() : "<null>";
    ImGui::TextUnformatted(val.c_str());
  }
  ImGui::EndTable();
}

void DrawWatchesSection(DebugMenuRegistry& debugReg) {
  if (!ImGui::CollapsingHeader("Live Watches & Tweaks", ImGuiTreeNodeFlags_DefaultOpen)) return;
  const auto& watches = debugReg.Watches();
  if (watches.empty()) {
    ImGui::TextDisabled("No active watches. Register with dev.watch('Label', fn)");
    return;
  }
  DrawWatchesTable(watches);
}

void DrawSimulationSection(EngineOptions& opts) {
  if (ImGui::CollapsingHeader("Simulation & Timing", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Pause Simulation", &opts.isPaused);
    ImGui::SameLine();
    if (ImGui::Button("Step Frame")) {
      opts.isPaused = true;
      opts.stepFrame = true;
    }

    ImGui::DragFloat("Time Scale", &opts.timeScale, 0.05f, 0.05f, 8.0f, "%.2fx");
    ImGui::DragInt("Sub-steps", &opts.simSubSteps, 0.2f, 1, 16);
    ImGui::DragFloat("Fixed Step dt", &opts.fixedStepDt, 0.001f, 0.001f, 0.1f, "%.4fs");

    if (ImGui::Checkbox("Deterministic Mode (Force Serial)", &opts.deterministic)) {
      ThreadPool::SetForceSerial(opts.deterministic);
    }
  }
}

void DrawDisplaySection(EngineOptions& opts) {
  if (ImGui::CollapsingHeader("Display & Window")) {
    static constexpr const char* kWindowModes[] = {"Windowed", "BorderlessFullscreen", "ExclusiveFullscreen"};
    int winMode = static_cast<int>(opts.windowMode);
    if (ImGui::Combo("Window Mode", &winMode, kWindowModes, 3)) {
      opts.windowMode = static_cast<WindowMode>(winMode);
    }

    static constexpr const char* kPresModes[] = {"Letterbox", "IntegerScale", "Stretch"};
    int presMode = static_cast<int>(opts.presentationMode);
    if (ImGui::Combo("Presentation Mode", &presMode, kPresModes, 3)) {
      opts.presentationMode = static_cast<PresentationMode>(presMode);
    }

    ImGui::Checkbox("VSync", &opts.vsync);
    ImGui::DragInt("FPS Target (0 = uncapped)", &opts.fpsTarget, 1.0f, 0, 360);
    ImGui::Checkbox("Perf Overlay", &opts.showPerfOverlay);
    ImGui::Checkbox("Draw Colliders Wireframe", &opts.drawColliders);
  }
}

void DrawAudioAndToolsSection(EngineOptions& opts) {
  if (ImGui::CollapsingHeader("Audio & Hot Reload")) {
    ImGui::SliderFloat("Master Volume", &opts.masterVolume, 0.0f, 1.0f);
    ImGui::Checkbox("Audio Enabled", &opts.audioEnabled);
    ImGui::DragFloat("Listener Radius", &opts.audioListenerRadius, 10.0f, 50.0f, 5000.0f);

    ImGui::Separator();
    ImGui::Checkbox("Hot Reload Enabled", &opts.hotReloadEnabled);
    ImGui::DragFloat("Poll Interval (s)", &opts.hotReloadPollSeconds, 0.05f, 0.1f, 5.0f);

    ImGui::Separator();
    ImGui::Checkbox("Show Dear ImGui Demo Window", &opts.showImGuiDemoWindow);
  }
}

}  // namespace

void DrawEngineOptionsPanel(Registry& registry) {
  auto* debugReg = registry.TryGet<DebugMenuRegistry>();
  if (debugReg == nullptr) return;

  auto& gameConfig = registry.Get<GameConfig>();
  auto& opts = gameConfig.GetEngineOptions();

  DrawWatchesSection(*debugReg);
  DrawSimulationSection(opts);
  DrawDisplaySection(opts);
  DrawAudioAndToolsSection(opts);
}

}  // namespace octarine::debug

#endif  // OCTARINE_WITH_IMGUI
