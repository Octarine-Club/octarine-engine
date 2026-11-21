#include "Debug/Panels/DebugUIPreviewerPanel.h"

#if defined(OCTARINE_WITH_IMGUI) && !defined(OCTARINE_SHIPPED)

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "AssetManager/AssetManager.h"
#include "Components/UIRectComponent.h"
#include "Debug/DebugMenuRegistry.h"
#include "ECS/Registry.h"
#include "Game/GameConfig.h"
#include "Systems/UILayoutMetrics.h"
#include "UI/UIPreviewer.h"
#include "UI/UIRuntime.h"
#include "imgui.h"
#include "imgui_stdlib.h"

namespace octarine::debug {
namespace {

bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) return true;
  auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), [](char a, char b) {
    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
  });
  return it != haystack.end();
}

void DrawToolbarActionRow(UIPreviewer& previewer, EngineOptions& opts) {
  if (ImGui::Button("Refresh Catalog")) {
    previewer.RefreshCatalogViews();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!previewer.IsActive());
  if (ImGui::Button("Reload Now")) {
    previewer.ReloadActivePreview();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Reload active UI view and mock sidecar (F5)");
  }
  ImGui::SameLine();
  if (ImGui::Button("Unmount")) {
    previewer.UnmountPreview(true);
  }
  ImGui::EndDisabled();

  ImGui::SameLine();
  ImGui::Checkbox("Auto Hot-Reload", &opts.hotReloadEnabled);

  ImGui::SameLine();
  bool isolated = previewer.IsIsolatedMode();
  if (ImGui::Checkbox("Isolate Canvas", &isolated)) {
    previewer.SetIsolatedMode(isolated, /*managePauseState=*/true);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Pause simulation, mute background scene UI, and render a backdrop");
  }

  ImGui::SameLine();
  static constexpr const char* kBackdrops[] = {"Scene Overlay", "Dark Slate", "Light Gray", "Checkerboard"};
  int backdropIdx = static_cast<int>(previewer.GetBackdropMode());
  ImGui::SetNextItemWidth(130.0f);
  if (ImGui::Combo("Backdrop", &backdropIdx, kBackdrops, 4)) {
    previewer.SetBackdropMode(static_cast<PreviewBackdropMode>(backdropIdx));
  }
}

void DrawToolbarOverlayAndScaleRow(Registry& registry, UIPreviewer& previewer, DebugMenuRegistry& debugReg,
                                   GameConfig& cfg, EngineOptions& opts) {
  auto* uiDbg = registry.TryGet<UIDebugConfig>();
  bool wireframes = (uiDbg != nullptr && uiDbg->enabled) || debugReg.ShowUILayoutBoxes();
  if (ImGui::Checkbox("Wireframe Bounds", &wireframes)) {
    debugReg.SetShowUILayoutBoxes(wireframes);
    if (uiDbg == nullptr) {
      uiDbg = &registry.Set<UIDebugConfig>(UIDebugConfig{});
    }
    uiDbg->enabled = wireframes;
  }
  ImGui::SameLine();
  bool clips = debugReg.ShowUIClipRects();
  if (ImGui::Checkbox("Clip Rects", &clips)) {
    debugReg.SetShowUIClipRects(clips);
  }
  ImGui::SameLine();
  bool focusHl = debugReg.HighlightUIFocus();
  if (ImGui::Checkbox("Focus Ring", &focusHl)) {
    debugReg.SetHighlightUIFocus(focusHl);
  }

  ImGui::SameLine();
  static constexpr const char* kPresModes[] = {"Letterbox", "IntegerScale", "Stretch", "Expand"};
  int presIdx = static_cast<int>(opts.presentationMode);
  ImGui::SetNextItemWidth(115.0f);
  if (ImGui::Combo("Presentation", &presIdx, kPresModes, 4)) {
    opts.presentationMode = static_cast<PresentationMode>(presIdx);
  }

  ImGui::SameLine();
  ImGui::SetNextItemWidth(90.0f);
  if (ImGui::SliderFloat("UI Scale", &opts.uiScale, 0.50f, 3.00f, "%.2fx")) {
    opts.uiScale = octarine::ui::ClampUserUiScale(opts.uiScale);
  }
  if (ImGui::IsItemHovered()) {
    const float eff = octarine::ui::ResolveUiScale(cfg);
    ImGui::SetTooltip("User UI scale multiplier (Effective scale: %.2fx, Logical: %dx%d). Right-click to reset.",
                      static_cast<double>(eff), cfg.windowWidth, cfg.windowHeight);
  }
  if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
    opts.uiScale = 1.0f;
  }

  ImGui::SameLine();
  if (ImGui::Button("Save PNG")) {
    const std::string base = cfg.HasLoadedConfig() ? cfg.GetAssetPath() : ".";
    const std::filesystem::path outPath = std::filesystem::path(base) / "ui_preview_capture.png";
    previewer.RequestCapture(outPath.string());
  }
  if (!previewer.LastCaptureStatus().empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", previewer.LastCaptureStatus().c_str());
  }
}

struct ViewportProfile {
  const char* label;
  int width;
  int height;
};

constexpr ViewportProfile kViewportProfiles[] = {
    {"Native Window", 0, 0},
    {"1920x1080 (16:9 FHD)", 1920, 1080},
    {"1280x800 (16:10 Deck)", 1280, 800},
    {"1280x720 (16:9 HD)", 1280, 720},
    {"2560x1080 (21:9 UW)", 2560, 1080},
    {"800x600 (4:3 Compact)", 800, 600},
    {"640x360 (16:9 Low)", 640, 360},
    {"Custom (WxH)", -1, -1},
};
constexpr int kCustomViewportProfileIdx = 7;

int FindActiveViewportProfileIndex(const UIPreviewer& previewer, int& customW, int& customH) {
  if (!previewer.HasSimulatedViewport()) return 0;
  const auto [simW, simH] = previewer.SimulatedViewport();
  customW = simW;
  customH = simH;
  for (int i = 1; i < kCustomViewportProfileIdx; ++i) {
    if (kViewportProfiles[i].width == simW && kViewportProfiles[i].height == simH) return i;
  }
  return kCustomViewportProfileIdx;
}

void DrawViewportComboControl(UIPreviewer& previewer, const int activeIdx, const int customW, const int customH) {
  ImGui::SetNextItemWidth(175.0f);
  if (!ImGui::BeginCombo("Viewport", kViewportProfiles[activeIdx].label)) return;
  for (int i = 0; i <= kCustomViewportProfileIdx; ++i) {
    if (!ImGui::Selectable(kViewportProfiles[i].label, i == activeIdx)) continue;
    if (i == 0) {
      previewer.ClearSimulatedViewport();
    } else if (i == kCustomViewportProfileIdx) {
      previewer.SetSimulatedViewport(std::clamp(customW, 64, 4096), std::clamp(customH, 64, 4096));
    } else {
      previewer.SetSimulatedViewport(kViewportProfiles[i].width, kViewportProfiles[i].height);
    }
  }
  ImGui::EndCombo();
}

void DrawCustomViewportControls(UIPreviewer& previewer, int& customW, int& customH) {
  const auto [simW, simH] = previewer.SimulatedViewport();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(64.0f);
  const bool wChanged = ImGui::InputInt("##sim_w", &customW, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  ImGui::TextUnformatted("x");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(64.0f);
  const bool hChanged = ImGui::InputInt("##sim_h", &customH, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
  if (wChanged || hChanged) {
    previewer.SetSimulatedViewport(std::clamp(customW, 64, 4096), std::clamp(customH, 64, 4096));
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Swap")) {
    previewer.SetSimulatedViewport(simH, simW);
  }
}

void DrawPresetSelectorCombo(UIPreviewer& previewer, const std::vector<std::string>& presets,
                             const std::string& activePreset, const char* comboId = "Preset",
                             const float width = 140.0f) {
  const char* comboLabel = activePreset.empty() ? (presets.empty() ? "(none)" : "default") : activePreset.c_str();
  ImGui::SetNextItemWidth(width);
  if (!ImGui::BeginCombo(comboId, comboLabel)) return;

  for (const auto& p : presets) {
    if (ImGui::Selectable(p.c_str(), p == activePreset)) previewer.SelectMockPreset(p);
  }
  if (ImGui::Selectable("@default", activePreset == "@default")) previewer.ApplyStressPreset("default");
  if (ImGui::Selectable("@empty", activePreset == "@empty")) previewer.ApplyStressPreset("empty");
  if (ImGui::Selectable("@overflow", activePreset == "@overflow")) previewer.ApplyStressPreset("overflow");
  ImGui::EndCombo();
}

void DrawToolbarViewportRow(UIPreviewer& previewer) {
  static int customW = 1280;
  static int customH = 800;

  const int activeIdx = FindActiveViewportProfileIndex(previewer, customW, customH);
  DrawViewportComboControl(previewer, activeIdx, customW, customH);
  if (previewer.HasSimulatedViewport()) {
    DrawCustomViewportControls(previewer, customW, customH);
  }

  ImGui::SameLine();
  bool safeAreas = previewer.ShowSafeAreaGuides();
  if (ImGui::Checkbox("Safe Areas", &safeAreas)) {
    previewer.SetShowSafeAreaGuides(safeAreas);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Toggle 93%% action-safe (cyan) and 90%% title-safe (gold) guides + center crosshair");
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!previewer.HasSimulatedViewport());
  bool intZoom = previewer.IntegerZoomSimulation();
  if (ImGui::Checkbox("Int Zoom", &intZoom)) {
    previewer.SetIntegerZoomSimulation(intZoom);
  }
  ImGui::EndDisabled();

  if (const auto presets = previewer.ListMockPresets(); !presets.empty()) {
    ImGui::SameLine();
    DrawPresetSelectorCombo(previewer, presets, previewer.ActiveMockPreset(), "Preset##toolbar", 120.0f);
  }
}

void DrawPrimaryToolbar(Registry& registry, UIPreviewer& previewer, DebugMenuRegistry& debugReg, GameConfig& cfg) {
  auto& opts = cfg.GetEngineOptions();
  DrawToolbarActionRow(previewer, opts);
  DrawToolbarOverlayAndScaleRow(registry, previewer, debugReg, cfg, opts);
  DrawToolbarViewportRow(previewer);
}

const char* MockSourceBadgeLabel(const MockSourceMode mode) {
  switch (mode) {
    case MockSourceMode::None:
      return "None";
    case MockSourceMode::ExplicitLua:
      return "Explicit Mock";
    case MockSourceMode::SidecarPreview:
      return "Sidecar";
    case MockSourceMode::AutoSeeded:
      return "Auto-Seeded";
  }
  return "None";
}

void DrawBannerIssueSummary(const std::vector<UILayoutIssue>& issues) {
  if (issues.empty()) {
    ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.55f, 1.0f), "| 0 Issues");
    return;
  }
  std::size_t errCount = 0;
  std::size_t warnCount = 0;
  for (const auto& issue : issues) {
    if (issue.severity == "error") {
      ++errCount;
    } else {
      ++warnCount;
    }
  }
  const ImVec4 badgeCol = (errCount > 0) ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f) : ImVec4(1.0f, 0.78f, 0.25f, 1.0f);
  ImGui::TextColored(badgeCol, "| %zu Error(s), %zu Warning(s)", errCount, warnCount);
}

void DrawDiagnosticsBanner(const UIPreviewer& previewer, const UIRuntime* uiRuntime,
                           const std::vector<UILayoutIssue>& issues) {
  if (uiRuntime != nullptr && !uiRuntime->GetDiagnostics().lastParseOk) {
    const auto& diag = uiRuntime->GetDiagnostics();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.42f, 0.10f, 0.12f, 0.90f));
    if (ImGui::BeginChild("##preview_err_banner", ImVec2(0.0f, 52.0f), ImGuiChildFlags_Borders)) {
      ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "Syntax Error (%s:%d)", diag.lastErrorFile.c_str(),
                         diag.lastErrorLine);
      ImGui::TextWrapped("%s", diag.lastError.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    return;
  }

  if (!previewer.LastMockError().empty()) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.42f, 0.16f, 0.10f, 0.90f));
    if (ImGui::BeginChild("##mock_err_banner", ImVec2(0.0f, 44.0f), ImGuiChildFlags_Borders)) {
      ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.40f, 1.0f), "Mock Script Error (preserving last valid tree)");
      ImGui::TextWrapped("%s", previewer.LastMockError().c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
  }

  if (previewer.IsActive()) {
    const std::uint64_t reloads = uiRuntime != nullptr ? uiRuntime->GetDiagnostics().reloadCount : 0;
    ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.55f, 1.0f), "Valid");
    ImGui::SameLine();
    ImGui::Text("| Source: %s | Reloads: %llu | Mock: %s", previewer.ActiveSource().c_str(),
                static_cast<unsigned long long>(reloads), MockSourceBadgeLabel(previewer.GetMockSourceMode()));
    if (previewer.IsMockModified()) {
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "[Modified]");
    }
    ImGui::SameLine();
    DrawBannerIssueSummary(issues);
  } else {
    ImGui::TextDisabled("No view mounted. Select an .oxml view from the left catalog or use Quick Scratchpad.");
  }
}

void DrawCatalogPane(UIPreviewer& previewer) {
  static char filterBuf[128] = "";
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputTextWithHint("##preview_filter", "Filter .oxml / .oss...", filterBuf, sizeof(filterBuf));
  const std::string filter(filterBuf);

  ImGui::SeparatorText("OXML Views");
  const auto& views = previewer.CatalogViews();
  if (views.empty()) {
    ImGui::TextDisabled("No .oxml files found in catalog.");
  }
  for (const auto& entry : views) {
    const std::string label = entry.displayPath.empty() ? entry.id : entry.displayPath;
    if (!ContainsCaseInsensitive(label, filter) && !ContainsCaseInsensitive(entry.id, filter)) continue;
    const bool selected =
        previewer.IsActive() && (previewer.ActiveSource() == entry.fullPath || previewer.ActiveSource() == entry.id);
    if (ImGui::Selectable(label.c_str(), selected)) {
      previewer.MountPreview(entry.fullPath.empty() ? entry.id : entry.fullPath);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("ID: %s\nPath: %s\nClick to mount in UI Previewer", entry.id.c_str(), entry.fullPath.c_str());
    }
  }

  if (ImGui::CollapsingHeader("OSS Stylesheets")) {
    const auto& styles = previewer.CatalogStyles();
    if (styles.empty()) {
      ImGui::TextDisabled("No .oss files found in catalog.");
    }
    for (const auto& entry : styles) {
      const std::string label = entry.displayPath.empty() ? entry.id : entry.displayPath;
      if (!ContainsCaseInsensitive(label, filter) && !ContainsCaseInsensitive(entry.id, filter)) continue;
      ImGui::TextUnformatted(label.c_str());
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("ID: %s\nPath: %s", entry.id.c_str(), entry.fullPath.c_str());
      }
    }
  }
}

void DrawAuditIssueElementCell(const UILayoutIssue& issue, const int rowIndex, DebugMenuRegistry& debugReg) {
  ImGui::PushID(rowIndex);
  const bool hasEntity = (issue.entity != Entity{});
  const bool selected = hasEntity && (debugReg.GetSelectedEntity() == issue.entity);
  if (ImGui::Selectable(issue.label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns) && hasEntity) {
    if (selected) {
      debugReg.SelectEntity(Entity{});
    } else {
      debugReg.SelectEntity(issue.entity);
      debugReg.SetHighlightSelectedEntity(true);
    }
  }
  if (ImGui::IsItemHovered() && hasEntity) {
    debugReg.SetHoveredEntity(issue.entity, issue.code + " | " + issue.label);
    ImGui::SetTooltip("Hover highlights in viewport; click to pin/unpin selection");
  }
  ImGui::PopID();
}

void DrawAuditTab(const std::vector<UILayoutIssue>& issues, DebugMenuRegistry& debugReg) {
  if (issues.empty()) {
    ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.55f, 1.0f), "No layout geometry or focus issues detected.");
    return;
  }
  if (!ImGui::BeginTable("##audit_tbl", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
    return;
  }
  ImGui::TableSetupColumn("Rule", ImGuiTableColumnFlags_WidthFixed, 145.0f);
  ImGui::TableSetupColumn("Element", ImGuiTableColumnFlags_WidthFixed, 150.0f);
  ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableHeadersRow();

  for (std::size_t i = 0; i < issues.size(); ++i) {
    const auto& issue = issues[i];
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    const ImVec4 sevColor =
        (issue.severity == "error") ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f) : ImVec4(1.0f, 0.78f, 0.25f, 1.0f);
    ImGui::TextColored(sevColor, "%s", issue.code.c_str());

    ImGui::TableSetColumnIndex(1);
    DrawAuditIssueElementCell(issue, static_cast<int>(i), debugReg);

    ImGui::TableSetColumnIndex(2);
    ImGui::TextWrapped("%s", issue.message.c_str());
  }
  ImGui::EndTable();
}

void DrawWatchedFilesTab(const UIRuntime* uiRuntime) {
  if (uiRuntime == nullptr) {
    ImGui::TextDisabled("UIRuntime unavailable.");
    return;
  }
  const auto& files = uiRuntime->GetDiagnostics().watchedFiles;
  if (files.empty()) {
    ImGui::TextDisabled("No disk files currently watched (inline XML or nothing mounted).");
    return;
  }
  for (const auto& f : files) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(f, ec);
    ImGui::TextColored(exists ? ImVec4(0.45f, 0.88f, 0.55f, 1.0f) : ImVec4(1.0f, 0.40f, 0.40f, 1.0f),
                       exists ? "[watch]" : "[missing]");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", f.c_str());
  }
}

bool TryParseHexColorRgba(std::string_view sv, float rgba[4]) {
  if (sv.size() < 7 || sv.front() != '#') return false;
  auto parseByte = [](const char hi, const char lo, float& out) -> bool {
    auto hexVal = [](const char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
      if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
      return -1;
    };
    const int a = hexVal(hi);
    const int b = hexVal(lo);
    if (a < 0 || b < 0) return false;
    out = static_cast<float>((a << 4) | b) / 255.0f;
    return true;
  };
  if (!parseByte(sv[1], sv[2], rgba[0]) || !parseByte(sv[3], sv[4], rgba[1]) || !parseByte(sv[5], sv[6], rgba[2])) {
    return false;
  }
  rgba[3] = 1.0f;
  if (sv.size() >= 9) {
    static_cast<void>(parseByte(sv[7], sv[8], rgba[3]));
  }
  return true;
}

std::string FormatHexColorRgba(const float rgba[4]) {
  auto toByte = [](const float f) -> unsigned int {
    return static_cast<unsigned int>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f);
  };
  char buf[16];
  if (toByte(rgba[3]) < 255U) {
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", toByte(rgba[0]), toByte(rgba[1]), toByte(rgba[2]),
                  toByte(rgba[3]));
  } else {
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", toByte(rgba[0]), toByte(rgba[1]), toByte(rgba[2]));
  }
  return buf;
}

void DrawMockStringControl(UIPreviewer& previewer, const MockFieldEntry& entry) {
  if (!entry.radioOptions.empty()) {
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("##radio_combo", entry.stringValue.c_str())) {
      for (const auto& opt : entry.radioOptions) {
        const bool selected = (opt == entry.stringValue);
        if (ImGui::Selectable(opt.c_str(), selected)) {
          previewer.SetMockFieldString(entry.path, opt);
        }
      }
      ImGui::EndCombo();
    }
    return;
  }
  if (entry.sampleHint == "color") {
    float rgba[4] = {0.29f, 0.33f, 0.41f, 1.0f};
    static_cast<void>(TryParseHexColorRgba(entry.stringValue, rgba));
    if (ImGui::ColorEdit4("##color", rgba, ImGuiColorEditFlags_NoInputs)) {
      previewer.SetMockFieldString(entry.path, FormatHexColorRgba(rgba));
    }
    ImGui::SameLine();
  }
  std::string buf = entry.stringValue;
  ImGui::SetNextItemWidth(-1.0f);
  const bool submitted = ImGui::InputText("##str", &buf, ImGuiInputTextFlags_EnterReturnsTrue);
  if (submitted || ImGui::IsItemDeactivatedAfterEdit()) {
    previewer.SetMockFieldString(entry.path, buf);
  }
}

std::string JoinCommaList(const std::vector<std::string>& items) {
  std::string out;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (i > 0) out += ", ";
    out += items[i];
  }
  return out;
}

void DrawMockNumberControl(UIPreviewer& previewer, const MockFieldEntry& entry) {
  double d = entry.numberValue;
  if (entry.hasSliderRange && entry.sliderMax > entry.sliderMin) {
    const double dMin = static_cast<double>(entry.sliderMin);
    const double dMax = static_cast<double>(entry.sliderMax);
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::SliderScalar("##sld", ImGuiDataType_Double, &d, &dMin, &dMax, "%.3g")) {
      previewer.SetMockFieldNumber(entry.path, d);
    }
  } else {
    ImGui::SetNextItemWidth(85.0f);
    if (ImGui::DragScalar("##drag", ImGuiDataType_Double, &d, 0.25f, nullptr, nullptr, "%.4g")) {
      previewer.SetMockFieldNumber(entry.path, d);
    }
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-1.0f);
  const bool submitted = ImGui::InputDouble("##num", &d, 0.0, 0.0, "%.6g", ImGuiInputTextFlags_EnterReturnsTrue);
  if (submitted || ImGui::IsItemDeactivatedAfterEdit()) {
    previewer.SetMockFieldNumber(entry.path, d);
  }
}

void DrawMockArrayControl(UIPreviewer& previewer, const MockFieldEntry& entry) {
  ImGui::Text("[%d item(s)]", entry.arrayLength);
  ImGui::SameLine();
  ImGui::BeginDisabled(entry.arrayLength <= 0);
  if (ImGui::SmallButton("-")) {
    previewer.ResizeMockArray(entry.path, entry.arrayLength - 1);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::SmallButton("+")) {
    previewer.ResizeMockArray(entry.path, entry.arrayLength + 1);
  }
}

void DrawMockScalarControl(UIPreviewer& previewer, const MockFieldEntry& entry) {
  if (entry.isUnset) {
    ImGui::TextDisabled("(unset)%s", entry.isConditionalOnly ? " [optional]" : "");
    ImGui::SameLine();
    if (ImGui::SmallButton("Seed")) previewer.SeedSingleContractField(entry.path);
    return;
  }
  if (entry.kind == MockFieldKind::Boolean) {
    bool b = entry.boolValue;
    if (ImGui::Checkbox("##bool", &b)) previewer.SetMockFieldBool(entry.path, b);
    return;
  }
  if (entry.kind == MockFieldKind::Number) {
    DrawMockNumberControl(previewer, entry);
    return;
  }
  if (entry.kind == MockFieldKind::Array) {
    DrawMockArrayControl(previewer, entry);
    return;
  }
  if (entry.kind == MockFieldKind::Table) {
    ImGui::TextDisabled("{table}");
    return;
  }
  DrawMockStringControl(previewer, entry);
}

void DrawMockFieldsTable(UIPreviewer& previewer, const std::vector<MockFieldEntry>& fields) {
  if (fields.empty()) {
    ImGui::TextDisabled("No context variables bound in this view.");
    return;
  }
  if (!ImGui::BeginTable("##mock_fields_tbl", 2,
                         ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
    return;
  }
  ImGui::TableSetupColumn("Binding Path", ImGuiTableColumnFlags_WidthFixed, 170.0f);
  ImGui::TableSetupColumn("Mock Value", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableHeadersRow();

  for (const auto& entry : fields) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    const bool isArrayElem = (entry.path.find('[') != std::string::npos);
    if (isArrayElem) ImGui::Indent(12.0f);
    if (entry.isUnset) {
      ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "%s", entry.path.c_str());
    } else {
      ImGui::TextUnformatted(entry.path.c_str());
    }
    if (isArrayElem) ImGui::Unindent(12.0f);

    ImGui::TableSetColumnIndex(1);
    ImGui::PushID(entry.path.c_str());
    DrawMockScalarControl(previewer, entry);
    ImGui::PopID();
  }
  ImGui::EndTable();
}

void DrawMockParamsSection(UIPreviewer& previewer, const std::vector<MockParamEntry>& params) {
  if (params.empty()) return;
  ImGui::SeparatorText("<Param> Values");
  for (const auto& p : params) {
    ImGui::PushID(p.name.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s%s:", p.name.c_str(), p.isOverridden ? " *" : "");
    ImGui::SameLine(160.0f);
    std::string buf = p.currentValue;
    ImGui::SetNextItemWidth(-1.0f);
    const bool submitted = ImGui::InputText("##param_val", &buf, ImGuiInputTextFlags_EnterReturnsTrue);
    if (submitted || ImGui::IsItemDeactivatedAfterEdit()) {
      previewer.SetMockParam(p.name, buf);
    }
    if (ImGui::IsItemHovered() && !p.defaultValue.empty()) {
      ImGui::SetTooltip("Default: %s", p.defaultValue.c_str());
    }
    ImGui::PopID();
  }
}

void DrawMockCallbacksAndMetaSection(UIPreviewer& previewer, const std::vector<MockCallbackEntry>& callbacks) {
  const auto& contract = previewer.ActiveContract();
  if (!callbacks.empty()) {
    ImGui::SeparatorText("Controller Callbacks");
    for (const auto& cb : callbacks) {
      ImGui::PushID(cb.name.c_str());
      ImGui::TextColored(
          cb.hasControllerHandler ? ImVec4(0.45f, 0.90f, 0.55f, 1.0f) : ImVec4(0.70f, 0.70f, 0.75f, 1.0f),
          cb.hasControllerHandler ? "[Bound]" : "[Stub]");
      ImGui::SameLine();
      ImGui::Text("%s (%s)", cb.name.c_str(), cb.sourceAttr.c_str());
      ImGui::SameLine();
      if (ImGui::SmallButton("Trigger")) {
        previewer.TriggerMockCallback(cb.name);
      }
      ImGui::PopID();
    }
  }
  if (!contract.refs.empty() || !contract.slots.empty()) {
    ImGui::SeparatorText("Contract Refs & Slots");
    if (!contract.refs.empty()) {
      const std::string joined = JoinCommaList(contract.refs);
      ImGui::TextDisabled("Refs (%zu): %s", contract.refs.size(), joined.c_str());
    }
    if (!contract.slots.empty()) {
      const std::string joined = JoinCommaList(contract.slots);
      ImGui::TextDisabled("Slots (%zu): %s", contract.slots.size(), joined.c_str());
    }
  }
}

enum class PendingCtrlSaveKind : std::uint8_t { None = 0, Sidecar = 1, Preset = 2, DeletePreset = 3 };

struct MockSaveModalState {
  PendingCtrlSaveKind kind{PendingCtrlSaveKind::None};
  std::string presetName{};
  bool setAsDefault{false};
};

void DrawMockControllerOverwritePopup(UIPreviewer& previewer, MockSaveModalState& modalState) {
  if (!ImGui::BeginPopupModal("##confirm_ctrl_overwrite", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
  ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "Controller Functions Detected");
  ImGui::TextWrapped(
      "The loaded mock script contains Lua controller functions which cannot be serialized cleanly and will be omitted "
      "when saving. Overwrite file on disk?");
  if (ImGui::Button("Overwrite Anyway")) {
    if (modalState.kind == PendingCtrlSaveKind::Sidecar) {
      previewer.SaveMockSidecar("", /*force=*/true);
    } else if (modalState.kind == PendingCtrlSaveKind::Preset) {
      previewer.SaveMockPreset(modalState.presetName, modalState.setAsDefault, /*force=*/true);
    } else if (modalState.kind == PendingCtrlSaveKind::DeletePreset) {
      previewer.DeleteMockPreset(modalState.presetName, /*force=*/true);
    }
    modalState.kind = PendingCtrlSaveKind::None;
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    modalState.kind = PendingCtrlSaveKind::None;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void RequestSaveSidecarWithGuard(UIPreviewer& previewer, MockSaveModalState& modalState) {
  if (previewer.HasLiveControllerScript() && !previewer.CanPreserveLiveControllerOnSave()) {
    modalState.kind = PendingCtrlSaveKind::Sidecar;
    ImGui::OpenPopup("##confirm_ctrl_overwrite");
  } else {
    previewer.SaveMockSidecar();
  }
}

void RequestSavePresetWithGuard(UIPreviewer& previewer, MockSaveModalState& modalState, const std::string& name,
                                const bool setDef) {
  if (name.empty()) return;
  if (previewer.HasLiveControllerScript() && !previewer.CanPreserveLiveControllerOnSave()) {
    modalState.kind = PendingCtrlSaveKind::Preset;
    modalState.presetName = name;
    modalState.setAsDefault = setDef;
    ImGui::OpenPopup("##confirm_ctrl_overwrite");
  } else {
    previewer.SaveMockPreset(name, setDef);
  }
}

void RequestDeletePresetWithGuard(UIPreviewer& previewer, MockSaveModalState& modalState, const std::string& name) {
  if (name.empty()) return;
  if (previewer.HasLiveControllerScript() && !previewer.CanPreserveLiveControllerOnSave()) {
    modalState.kind = PendingCtrlSaveKind::DeletePreset;
    modalState.presetName = name;
    ImGui::OpenPopup("##confirm_ctrl_overwrite");
  } else {
    previewer.DeleteMockPreset(name);
  }
}

void DrawMockPresetBar(UIPreviewer& previewer, const bool canSave, MockSaveModalState& modalState) {
  static char savePresetBuf[64] = "compact";
  static bool setAsDefault = false;

  const auto presets = previewer.ListMockPresets();
  const std::string& activePreset = previewer.ActiveMockPreset();
  DrawPresetSelectorCombo(previewer, presets, activePreset);

  ImGui::SameLine();
  if (ImGui::SmallButton("Stress: Empty")) previewer.ApplyStressPreset("empty");
  ImGui::SameLine();
  if (ImGui::SmallButton("Stress: Overflow")) previewer.ApplyStressPreset("overflow");

  const bool canDelete = canSave && !activePreset.empty() && !activePreset.starts_with('@') && presets.size() > 1;
  ImGui::SameLine();
  ImGui::BeginDisabled(!canDelete);
  if (ImGui::SmallButton("Del Preset")) RequestDeletePresetWithGuard(previewer, modalState, activePreset);
  ImGui::EndDisabled();

  ImGui::BeginDisabled(!canSave);
  ImGui::SetNextItemWidth(130.0f);
  ImGui::InputTextWithHint("##save_preset_name", "Preset name...", savePresetBuf, sizeof(savePresetBuf));
  ImGui::SameLine();
  ImGui::Checkbox("Default", &setAsDefault);
  ImGui::SameLine();
  if (ImGui::Button("Save Preset") && savePresetBuf[0] != '\0') {
    RequestSavePresetWithGuard(previewer, modalState, savePresetBuf, setAsDefault);
  }
  ImGui::EndDisabled();
}

void DrawMockScriptLoadRow(UIPreviewer& previewer) {
  static char loadPathBuf[256] = "";
  ImGui::SetNextItemWidth(220.0f);
  ImGui::InputTextWithHint("##load_mock_path", "Load mock .lua script...", loadPathBuf, sizeof(loadPathBuf));
  ImGui::SameLine();
  if (ImGui::Button("Load Mock") && loadPathBuf[0] != '\0') {
    previewer.LoadMockScript(loadPathBuf);
  }
}

void DrawMockHeaderBar(UIPreviewer& previewer, const bool hasAnyUnset, MockSaveModalState& modalState) {
  const bool canSave =
      !previewer.IsInline() && (!previewer.TargetSidecarPath().empty() || !previewer.ActiveMockPath().empty());

  ImGui::Text("[%s]", MockSourceBadgeLabel(previewer.GetMockSourceMode()));
  if (previewer.IsMockModified()) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "Modified");
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!canSave);
  if (ImGui::Button("Save .preview.lua")) RequestSaveSidecarWithGuard(previewer, modalState);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Copy Mock Lua")) {
    const std::string luaText = previewer.ExportMockLuaScript();
    if (!luaText.empty()) ImGui::SetClipboardText(luaText.c_str());
  }
  ImGui::SameLine();
  if (ImGui::Button("Use Auto-Seeded")) previewer.ResetToAutoSeeded();
  if (!previewer.ActiveMockPath().empty()) {
    ImGui::SameLine();
    if (ImGui::Button("Reload Mock")) previewer.LoadMockScript(previewer.ActiveMockPath());
  }
  if (hasAnyUnset) {
    ImGui::SameLine();
    if (ImGui::Button("Seed Missing")) previewer.SeedMissingContractFields();
  }

  DrawMockScriptLoadRow(previewer);
}

void DrawMockDataTab(UIPreviewer& previewer) {
  if (!previewer.IsActive()) {
    ImGui::TextDisabled("Mount an .oxml view to inspect and edit its mock contract.");
    return;
  }

  const auto fields = previewer.CollectMockFields();
  const auto params = previewer.CollectMockParams();
  const auto callbacks = previewer.CollectMockCallbacks();
  const bool hasAnyUnset = std::ranges::any_of(fields, [](const MockFieldEntry& f) { return f.isUnset; });
  static MockSaveModalState modalState{};
  const bool canSave =
      !previewer.IsInline() && (!previewer.TargetSidecarPath().empty() || !previewer.ActiveMockPath().empty());

  if (previewer.HasLiveControllerScript()) {
    ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f),
                       "Controller functions detected: saving or deleting presets will rewrite data-only Lua.");
  }
  DrawMockHeaderBar(previewer, hasAnyUnset, modalState);
  DrawMockPresetBar(previewer, canSave, modalState);
  DrawMockControllerOverwritePopup(previewer, modalState);

  ImGui::Separator();
  if (ImGui::BeginChild("##mock_data_scroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None)) {
    DrawMockFieldsTable(previewer, fields);
    DrawMockParamsSection(previewer, params);
    DrawMockCallbacksAndMetaSection(previewer, callbacks);
  }
  ImGui::EndChild();
}

void DrawCallbackEventsTab(UIPreviewer& previewer) {
  static bool autoScroll = true;
  static std::size_t lastSeenCount = 0;

  if (ImGui::SmallButton("Clear Log")) {
    previewer.ClearCallbackEvents();
    lastSeenCount = 0;
  }
  ImGui::SameLine();
  ImGui::Checkbox("Auto-scroll", &autoScroll);
  ImGui::Separator();

  const auto& events = previewer.RecentCallbackEvents();
  if (events.empty()) {
    ImGui::TextDisabled("No controller callbacks fired yet (click buttons or focus widgets to test).");
    lastSeenCount = 0;
    return;
  }
  if (ImGui::BeginChild("##event_log_scroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None)) {
    for (const auto& ev : events) {
      ImGui::BulletText("%s", ev.c_str());
    }
    if (autoScroll && events.size() != lastSeenCount) {
      ImGui::SetScrollHereY(1.0f);
    }
    lastSeenCount = events.size();
  }
  ImGui::EndChild();
}

void DrawScratchpadTab(UIPreviewer& previewer) {
  static char xmlBuf[4096] =
      "<Canvas>\n"
      "  <Panel width=\"360px\" padding=\"16px\" align=\"center\" justify=\"center\">\n"
      "    <Label text=\"UI Scratchpad Preview\" />\n"
      "    <Button text=\"Test Action\" on-click=\"onTestAction\" />\n"
      "  </Panel>\n"
      "</Canvas>\n";
  static char ossBuf[2048] =
      "Panel { background-color: #202330; border-color: #6c5ce7; border-width: 2; gap: 10px; }\n"
      "Button { background-color: #3a3f58; padding: 8px 16px; }\n"
      "Button:hover { background-color: #565d82; }\n"
      "Button:focus { border-color: #ffd166; border-width: 2; }\n";

  if (ImGui::Button("Apply Scratchpad")) {
    previewer.MountInlineXml(xmlBuf, ossBuf);
  }
  ImGui::TextDisabled("OXML Markup:");
  ImGui::InputTextMultiline("##scratch_oxml", xmlBuf, sizeof(xmlBuf), ImVec2(-1.0f, 140.0f));
  ImGui::TextDisabled("Inline OSS Rules:");
  ImGui::InputTextMultiline("##scratch_oss", ossBuf, sizeof(ossBuf), ImVec2(-1.0f, 100.0f));
}

void DrawInspectorPane(UIPreviewer& previewer, const UIRuntime* uiRuntime, DebugMenuRegistry& debugReg,
                       const std::vector<UILayoutIssue>& issues) {
  if (!ImGui::BeginTabBar("##previewer_right_tabs")) return;
  if (ImGui::BeginTabItem("Layout Audit")) {
    DrawAuditTab(issues, debugReg);
    ImGui::EndTabItem();
  }
  if (ImGui::BeginTabItem("Mock Data")) {
    DrawMockDataTab(previewer);
    ImGui::EndTabItem();
  }
  if (ImGui::BeginTabItem("Watched Files")) {
    DrawWatchedFilesTab(uiRuntime);
    ImGui::EndTabItem();
  }
  if (ImGui::BeginTabItem("Event Log")) {
    DrawCallbackEventsTab(previewer);
    ImGui::EndTabItem();
  }
  if (ImGui::BeginTabItem("Quick Scratchpad")) {
    DrawScratchpadTab(previewer);
    ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
}

}  // namespace

void DrawUIPreviewerPanel(Registry& registry) {
  auto* debugReg = registry.TryGet<DebugMenuRegistry>();
  auto* cfg = registry.TryGet<GameConfig>();
  if (debugReg == nullptr || cfg == nullptr) return;

  auto* previewer = registry.TryGet<UIPreviewer>();
  if (previewer == nullptr) {
    previewer = &registry.Set<UIPreviewer>(UIPreviewer(&registry));
    previewer->RefreshCatalogViews();
  } else {
    previewer->SetRegistry(&registry);
    if (previewer->CatalogViews().empty() && previewer->CatalogStyles().empty()) {
      previewer->RefreshCatalogViews();
    }
  }
  auto* uiRuntime = registry.TryGet<UIRuntime>();

  DrawPrimaryToolbar(registry, *previewer, *debugReg, *cfg);
  ImGui::Separator();

  const auto issues = previewer->AuditLayout();
  DrawDiagnosticsBanner(*previewer, uiRuntime, issues);
  ImGui::Separator();

  const float availW = ImGui::GetContentRegionAvail().x;
  const float leftW = std::clamp(availW * 0.34f, 180.0f, 340.0f);

  if (ImGui::BeginChild("##previewer_left_pane", ImVec2(leftW, 0.0f),
                        ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {
    DrawCatalogPane(*previewer);
  }
  ImGui::EndChild();

  ImGui::SameLine();

  if (ImGui::BeginChild("##previewer_right_pane", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
    DrawInspectorPane(*previewer, uiRuntime, *debugReg, issues);
  }
  ImGui::EndChild();
}

}  // namespace octarine::debug

#endif  // OCTARINE_WITH_IMGUI && !OCTARINE_SHIPPED
