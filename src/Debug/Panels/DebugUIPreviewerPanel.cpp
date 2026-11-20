#include "Debug/Panels/DebugUIPreviewerPanel.h"

#if defined(OCTARINE_WITH_IMGUI) && !defined(OCTARINE_SHIPPED)

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
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

void DrawPrimaryToolbar(Registry& registry, UIPreviewer& previewer, DebugMenuRegistry& debugReg, GameConfig& cfg) {
  auto& opts = cfg.GetEngineOptions();
  DrawToolbarActionRow(previewer, opts);
  DrawToolbarOverlayAndScaleRow(registry, previewer, debugReg, cfg, opts);
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

  if (previewer.IsActive()) {
    const std::uint64_t reloads = uiRuntime != nullptr ? uiRuntime->GetDiagnostics().reloadCount : 0;
    ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.55f, 1.0f), "Valid");
    ImGui::SameLine();
    ImGui::Text("| Source: %s | Reloads: %llu |", previewer.ActiveSource().c_str(),
                static_cast<unsigned long long>(reloads));
    ImGui::SameLine();
    if (issues.empty()) {
      ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.55f, 1.0f), "0 Layout Warnings");
    } else {
      ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "%zu Layout Warning(s)", issues.size());
    }
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
    ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "%s", issue.code.c_str());

    ImGui::TableSetColumnIndex(1);
    ImGui::PushID(static_cast<int>(i));
    const bool selected = (debugReg.GetSelectedEntity() == issue.entity);
    if (ImGui::Selectable(issue.label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
      debugReg.SelectEntity(issue.entity);
      debugReg.SetHighlightSelectedEntity(true);
    }
    ImGui::PopID();

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

void DrawCallbackEventsTab(UIPreviewer& previewer) {
  if (ImGui::SmallButton("Clear Log")) {
    previewer.ClearCallbackEvents();
  }
  ImGui::Separator();
  const auto& events = previewer.RecentCallbackEvents();
  if (events.empty()) {
    ImGui::TextDisabled("No controller callbacks fired yet (click buttons or focus widgets to test).");
    return;
  }
  for (auto it = events.rbegin(); it != events.rend(); ++it) {
    ImGui::BulletText("%s", it->c_str());
  }
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
