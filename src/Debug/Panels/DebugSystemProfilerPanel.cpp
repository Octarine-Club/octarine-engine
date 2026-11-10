#include "Debug/Panels/DebugSystemProfilerPanel.h"

#ifdef OCTARINE_WITH_IMGUI
#include <algorithm>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

#include "Debug/SystemProfilerTracker.h"
#include "ECS/Registry.h"
#include "Engine/EngineContext.h"
#include "General/PerformanceTracker.h"
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

SystemProfilerTracker& GetProfilerTracker() {
  static SystemProfilerTracker tracker;
  return tracker;
}

struct ProfilerFilterState {
  char searchBuf[64] = "";
  bool hideIdle = false;  // Hide systems with Avg < 1.0 us

  // Summary sections
  bool showFrameTelemetry = true;
  bool showSparkline = true;

  // Table metric columns
  bool showLastUs = false;
  bool showAvgUs = true;
  bool showP95Us = true;
  bool showPeakUs = true;
  bool showAvgMs = true;
  bool showPctUpdate = true;
};

ProfilerFilterState& GetFilterState() {
  static ProfilerFilterState state;
  return state;
}

void DrawProfilerToolbar(Registry& registry, SystemProfilerTracker& tracker, ProfilerFilterState& filters) {
  auto& invResult = GetInvariantResult();

  bool isPaused = tracker.IsPaused();
  if (ImGui::Checkbox("Pause", &isPaused)) {
    tracker.SetPaused(isPaused);
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Freeze metrics to inspect steady-state performance");

  ImGui::SameLine();
  if (ImGui::Button("Reset Peaks")) {
    tracker.Reset();
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear recorded peak (max) watermarks and history");

  ImGui::SameLine();
  static constexpr const char* kRefreshOptions[] = {"Every Frame", "100 ms", "250 ms (Default)", "500 ms", "1.0 s"};
  static constexpr float kRefreshIntervals[] = {0.0f, 0.1f, 0.25f, 0.5f, 1.0f};
  static int currentRefreshIdx = 2;

  ImGui::SetNextItemWidth(150.0f);
  if (ImGui::Combo("Refresh", &currentRefreshIdx, kRefreshOptions, 5)) {
    tracker.SetRefreshInterval(kRefreshIntervals[currentRefreshIdx]);
  }

  ImGui::SameLine();
  if (ImGui::BeginCombo("Metrics", "Columns & Panels")) {
    ImGui::TextDisabled("Summary Panels");
    ImGui::Checkbox("Frame Telemetry", &filters.showFrameTelemetry);
    ImGui::Checkbox("Update Sparkline", &filters.showSparkline);
    ImGui::Separator();
    ImGui::TextDisabled("Table Columns");
    ImGui::Checkbox("Last (us)", &filters.showLastUs);
    ImGui::Checkbox("Avg (us)", &filters.showAvgUs);
    ImGui::Checkbox("P95 (us)", &filters.showP95Us);
    ImGui::Checkbox("Peak (us)", &filters.showPeakUs);
    ImGui::Checkbox("Avg (ms)", &filters.showAvgMs);
    ImGui::Checkbox("% Update", &filters.showPctUpdate);
    ImGui::EndCombo();
  }

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

  ImGui::SetNextItemWidth(180.0f);
  ImGui::InputTextWithHint("##filter_systems", "Filter systems...", filters.searchBuf, sizeof(filters.searchBuf));
  ImGui::SameLine();
  ImGui::Checkbox("Hide Idle (< 1 us)", &filters.hideIdle);
}

void DrawProfilerSummary(const Registry& registry, const ProfilerSnapshot& snap, const ProfilerFilterState& filters) {
  if (filters.showFrameTelemetry) {
    const auto* ctx = registry.TryGet<EngineContext>();
    const PerformanceStats* stats = ctx != nullptr ? ctx->perfStats : nullptr;

    if (stats != nullptr) {
      ImGui::Text(
          "FPS: %.1f (Avg: %.1f | P50: %.1f | P95: %.1f | P99: %.1f) | Frame: %.2f ms (Avg: %.2f ms | P95: %.2f ms)",
          static_cast<double>(stats->fps), static_cast<double>(stats->avgFps), static_cast<double>(stats->p50Fps),
          static_cast<double>(stats->p95Fps), static_cast<double>(stats->p99Fps),
          static_cast<double>(stats->frameTimeMs), static_cast<double>(stats->avgFrameTimeMs),
          static_cast<double>(stats->p95FrameTimeMs));
      ImGui::Text("Memory: %.2f MB | Entities: %llu | Archetypes: %zu", stats->memoryMb,
                  static_cast<unsigned long long>(registry.GetUserEntityCount()), registry.GetArchetypeCount());
    } else {
      ImGui::Text("Entities: %llu | Archetypes: %zu", static_cast<unsigned long long>(registry.GetUserEntityCount()),
                  registry.GetArchetypeCount());
    }

    ImGui::Text("ECS Update: Avg %.3f ms (P95: %.3f ms | Peak: %.3f ms) | Flush: Avg %.1f us (Peak: %.1f us)",
                snap.avgUpdateMs, snap.p95UpdateMs, snap.maxUpdateMs, snap.avgFlushUs, snap.maxFlushUs);
  }

  if (filters.showSparkline && !snap.updateMsHistory.empty()) {
    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "Update: %.3f ms (Avg: %.3f ms)", snap.lastUpdateMs, snap.avgUpdateMs);
    ImGui::PlotLines("##update_sparkline", snap.updateMsHistory.data(), static_cast<int>(snap.updateMsHistory.size()),
                     0, overlay, 0.0f, static_cast<float>(std::max(0.001, snap.maxUpdateMs * 1.15)),
                     ImVec2(-1.0f, 40.0f));
  }
}

void DrawSystemsTable(const ProfilerSnapshot& snap, const ProfilerFilterState& filters) {
  constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY |
                                          ImGuiTableFlags_Sortable | ImGuiTableFlags_Hideable;

  int columnCount = 2;  // Order and Name are always present
  if (filters.showLastUs) ++columnCount;
  if (filters.showAvgUs) ++columnCount;
  if (filters.showP95Us) ++columnCount;
  if (filters.showPeakUs) ++columnCount;
  if (filters.showAvgMs) ++columnCount;
  if (filters.showPctUpdate) ++columnCount;

  if (ImGui::BeginTable("##systems_profiler_table", columnCount, kTableFlags)) {
    ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 45.0f);
    ImGui::TableSetupColumn("System Name", ImGuiTableColumnFlags_WidthStretch);
    if (filters.showLastUs) ImGui::TableSetupColumn("Last (us)", ImGuiTableColumnFlags_WidthFixed, 75.0f);
    if (filters.showAvgUs) ImGui::TableSetupColumn("Avg (us)", ImGuiTableColumnFlags_WidthFixed, 75.0f);
    if (filters.showP95Us) ImGui::TableSetupColumn("P95 (us)", ImGuiTableColumnFlags_WidthFixed, 75.0f);
    if (filters.showPeakUs) ImGui::TableSetupColumn("Peak (us)", ImGuiTableColumnFlags_WidthFixed, 75.0f);
    if (filters.showAvgMs) ImGui::TableSetupColumn("Avg (ms)", ImGuiTableColumnFlags_WidthFixed, 75.0f);
    if (filters.showPctUpdate) ImGui::TableSetupColumn("% Update", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableHeadersRow();

    std::vector<const SystemTimingStats*> displaySystems;
    displaySystems.reserve(snap.systems.size());
    const std::string filterStr(filters.searchBuf);

    for (const auto& sys : snap.systems) {
      if (filters.hideIdle && sys.avgUs < 1.0) continue;
      if (!filterStr.empty() && sys.name.find(filterStr) == std::string::npos) continue;
      displaySystems.push_back(&sys);
    }

    if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
      if (sortSpecs->SpecsCount > 0 && sortSpecs->SpecsDirty) {
        const auto& spec = sortSpecs->Specs[0];
        const bool asc = (spec.SortDirection == ImGuiSortDirection_Ascending);

        std::sort(displaySystems.begin(), displaySystems.end(),
                  [&](const SystemTimingStats* a, const SystemTimingStats* b) {
                    int col = spec.ColumnIndex;
                    if (col == 0) return asc ? (a->orderIndex < b->orderIndex) : (a->orderIndex > b->orderIndex);
                    if (col == 1) return asc ? (a->name < b->name) : (a->name > b->name);
                    return asc ? (a->avgUs < b->avgUs) : (a->avgUs > b->avgUs);
                  });
      }
    }

    for (const auto* sys : displaySystems) {
      ImGui::TableNextRow();
      int colIdx = 0;

      ImGui::TableSetColumnIndex(colIdx++);
      ImGui::Text("%zu", sys->orderIndex);

      ImGui::TableSetColumnIndex(colIdx++);
      ImGui::TextUnformatted(sys->name.c_str());
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("System: %s", sys->name.c_str());
        ImGui::Separator();
        ImGui::Text("Last: %.1f us", sys->lastUs);
        ImGui::Text("Avg:  %.1f us (%.3f ms)", sys->avgUs, sys->avgUs / 1000.0);
        ImGui::Text("P95:  %.1f us", sys->p95Us);
        ImGui::Text("Peak: %.1f us", sys->maxUs);
        ImGui::Text("Share of Update: %.1f%%", static_cast<double>(sys->shareOfUpdate * 100.0f));
        ImGui::EndTooltip();
      }

      if (filters.showLastUs) {
        ImGui::TableSetColumnIndex(colIdx++);
        ImGui::Text("%.1f", sys->lastUs);
      }

      if (filters.showAvgUs) {
        ImGui::TableSetColumnIndex(colIdx++);
        ImGui::Text("%.1f", sys->avgUs);
      }

      if (filters.showP95Us) {
        ImGui::TableSetColumnIndex(colIdx++);
        ImGui::Text("%.1f", sys->p95Us);
      }

      if (filters.showPeakUs) {
        ImGui::TableSetColumnIndex(colIdx++);
        ImGui::Text("%.1f", sys->maxUs);
      }

      if (filters.showAvgMs) {
        ImGui::TableSetColumnIndex(colIdx++);
        ImGui::Text("%.3f", sys->avgUs / 1000.0);
      }

      if (filters.showPctUpdate) {
        ImGui::TableSetColumnIndex(colIdx++);
        char overlay[32];
        std::snprintf(overlay, sizeof(overlay), "%.1f%%", static_cast<double>(sys->shareOfUpdate * 100.0f));
        ImGui::ProgressBar(sys->shareOfUpdate, ImVec2(-1.0f, 0.0f), overlay);
      }
    }
    ImGui::EndTable();
  }
}

}  // namespace

void DrawSystemProfilerPanel(Registry& registry) {
  if (!registry.IsSystemTimingEnabled()) {
    registry.SetSystemTimingEnabled(true);
  }

  auto& tracker = GetProfilerTracker();
  tracker.RecordFrame(registry, ImGui::GetIO().DeltaTime);

  auto& filters = GetFilterState();
  const auto& snap = tracker.GetSnapshot();

  DrawProfilerToolbar(registry, tracker, filters);
  ImGui::Separator();
  DrawProfilerSummary(registry, snap, filters);
  ImGui::Separator();
  DrawSystemsTable(snap, filters);
}

}  // namespace octarine::debug

#endif  // OCTARINE_WITH_IMGUI
