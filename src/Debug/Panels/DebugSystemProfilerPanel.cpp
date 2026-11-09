#include "Debug/Panels/DebugSystemProfilerPanel.h"

#ifdef OCTARINE_WITH_IMGUI
#include <algorithm>
#include <numeric>
#include <string>
#include <vector>

#include "ECS/Registry.h"
#include "imgui.h"

namespace octarine::debug {

namespace {

struct InvariantCheckResult {
  bool performed = false;
  bool ok = false;
  std::string error{};
};

InvariantCheckResult& GetInvariantResult() {
  static InvariantCheckResult res;
  return res;
}

void DrawProfilerSummary(Registry& registry, uint64_t totalNs, uint64_t flushNs) {
  auto& invResult = GetInvariantResult();

  ImGui::Text("Total Update: %.3f ms | Flush: %.2f µs | Entities: %llu | Archetypes: %zu",
              static_cast<double>(totalNs) / 1'000'000.0, static_cast<double>(flushNs) / 1'000.0,
              static_cast<unsigned long long>(registry.GetUserEntityCount()), registry.GetArchetypeCount());

  ImGui::SameLine();
  if (ImGui::Button("Validate Invariants")) {
    invResult.performed = true;
    try {
      registry.ValidateInvariants();
      invResult.ok = true;
      invResult.error.clear();
    } catch (const std::exception& ex) {
      invResult.ok = false;
      invResult.error = ex.what();
    }
  }

  if (invResult.performed) {
    ImGui::SameLine();
    if (invResult.ok) {
      ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Invariants OK");
    } else {
      ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "FAILED: %s", invResult.error.c_str());
    }
  }
}

void DrawSystemsTable(Registry& registry, const std::vector<uint64_t>& timings, uint64_t totalNs) {
  constexpr ImGuiTableFlags kTableFlags =
      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY;

  if (ImGui::BeginTable("##systems_profiler_table", 5, kTableFlags)) {
    ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed, 45.0f);
    ImGui::TableSetupColumn("System Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Time (µs)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("% Frame", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableHeadersRow();

    const size_t count = registry.GetSystemCount();
    for (size_t i = 0; i < count; ++i) {
      const uint64_t ns = (i < timings.size()) ? timings[i] : 0;
      const float us = static_cast<float>(ns) / 1000.0f;
      const float ms = us / 1000.0f;
      const float pct = (totalNs > 0) ? static_cast<float>(ns) / static_cast<float>(totalNs) : 0.0f;

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::Text("%zu", i);

      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(registry.GetSystemName(static_cast<SystemId>(i)).c_str());

      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%.1f", static_cast<double>(us));

      ImGui::TableSetColumnIndex(3);
      ImGui::Text("%.3f", static_cast<double>(ms));

      ImGui::TableSetColumnIndex(4);
      char overlay[32];
      std::snprintf(overlay, sizeof(overlay), "%.1f%%", static_cast<double>(pct * 100.0f));
      ImGui::ProgressBar(pct, ImVec2(-1.0f, 0.0f), overlay);
    }
    ImGui::EndTable();
  }
}

}  // namespace

void DrawSystemProfilerPanel(Registry& registry) {
  if (!registry.IsSystemTimingEnabled()) {
    registry.SetSystemTimingEnabled(true);
  }

  const auto& timings = registry.GetLastSystemTimingsNs();
  const uint64_t flushNs = registry.GetLastFlushTimingNs();
  const uint64_t totalNs = std::accumulate(timings.begin(), timings.end(), flushNs);

  DrawProfilerSummary(registry, totalNs, flushNs);
  ImGui::Separator();
  DrawSystemsTable(registry, timings, totalNs);
}

}  // namespace octarine::debug

#endif  // OCTARINE_WITH_IMGUI
