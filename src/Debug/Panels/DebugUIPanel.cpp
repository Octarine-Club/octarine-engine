#include "Debug/Panels/DebugUIPanel.h"

#ifdef OCTARINE_WITH_IMGUI
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "Components/NameComponent.h"
#include "Components/UIRectComponent.h"
#include "Components/UIZIndexComponent.h"
#include "Debug/DebugMenuRegistry.h"
#include "ECS/Registry.h"
#include "UI/UIRuntime.h"
#include "imgui.h"

namespace octarine::debug {

namespace {

void DrawOverlayControls(DebugMenuRegistry& debugReg) {
  bool layout = debugReg.ShowUILayoutBoxes();
  if (ImGui::Checkbox("Show UI Layout Boxes (Cyan)", &layout)) {
    debugReg.SetShowUILayoutBoxes(layout);
  }
  ImGui::SameLine();
  bool clip = debugReg.ShowUIClipRects();
  if (ImGui::Checkbox("Show Clip Scissor Rects (Magenta)", &clip)) {
    debugReg.SetShowUIClipRects(clip);
  }
  ImGui::SameLine();
  bool focus = debugReg.HighlightUIFocus();
  if (ImGui::Checkbox("Highlight Focused Widget (Gold)", &focus)) {
    debugReg.SetHighlightUIFocus(focus);
  }
}

void DrawMountedViewsSection(Registry& registry) {
  auto* uiRuntime = registry.TryGet<UIRuntime>();
  if (uiRuntime == nullptr) {
    ImGui::TextDisabled("UIRuntime is not active.");
    return;
  }

  const auto views = uiRuntime->GetMountedViewsDebugInfo();
  ImGui::Text("Mounted Views: %zu", views.size());
  ImGui::SameLine();
  if (ImGui::SmallButton("Reload All Views")) {
    uiRuntime->ReloadAllMountedViews();
  }

  if (ImGui::BeginTable("##mounted_views_tbl", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Watched Files", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 75.0f);
    ImGui::TableHeadersRow();

    for (const auto& v : views) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::Text("%llu", static_cast<unsigned long long>(v.id));

      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(v.sourcePath.c_str());

      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%zu file(s)", v.watchedFiles.size());
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        for (const auto& f : v.watchedFiles) ImGui::TextUnformatted(f.c_str());
        ImGui::EndTooltip();
      }

      ImGui::TableSetColumnIndex(3);
      ImGui::PushID(static_cast<int>(v.id));
      if (ImGui::SmallButton("Reload")) {
        uiRuntime->ReloadMountedView(v.id);
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

struct UIRectDisplayEntry {
  std::string label{};
  std::string posStr{};
  std::string sizeStr{};
  int z = 0;
};

struct UIElementCache {
  float timer = 0.0f;
  std::vector<std::pair<Entity, UIRectDisplayEntry>> entries{};
};

UIElementCache& GetUIElementCache() {
  static UIElementCache cache;
  return cache;
}

void DrawUIElementsSection(Registry& registry, DebugMenuRegistry& debugReg) {
  ImGui::TextDisabled("Live UI Rect Elements in Scene:");

  auto& cache = GetUIElementCache();
  const float dt = ImGui::GetIO().DeltaTime;
  cache.timer -= dt;

  if (cache.timer <= 0.0f) {
    cache.timer = 0.25f;  // Stabilize at 4 Hz
    cache.entries.clear();

    for (const auto ent : registry.GetUserEntities()) {
      if (!registry.HasComponent<UIRectComponent>(ent)) continue;
      const auto& rect = registry.GetComponent<UIRectComponent>(ent);

      std::string label = "Entity " + std::to_string(ent.GetId());
      if (registry.HasComponent<NameComponent>(ent)) {
        label += " (" + registry.GetComponent<NameComponent>(ent).name + ")";
      }

      char posBuf[32];
      std::snprintf(posBuf, sizeof(posBuf), "%.1f, %.1f", static_cast<double>(rect.left),
                    static_cast<double>(rect.top));

      char sizeBuf[32];
      std::snprintf(sizeBuf, sizeof(sizeBuf), "%.1f, %.1f", static_cast<double>(rect.Width()),
                    static_cast<double>(rect.Height()));

      int z = 0;
      if (registry.HasComponent<UIZIndexComponent>(ent)) {
        z = registry.GetComponent<UIZIndexComponent>(ent).z;
      }

      cache.entries.push_back({ent, UIRectDisplayEntry{std::move(label), posBuf, sizeBuf, z}});
    }
  }

  if (ImGui::BeginTable("##ui_elements_tbl", 4,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
    ImGui::TableSetupColumn("Entity", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Pos (x, y)", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("Size (w, h)", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("Z", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    ImGui::TableHeadersRow();

    for (const auto& [ent, item] : cache.entries) {
      if (!registry.IsAlive(ent)) continue;

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);

      const bool selected = (debugReg.GetSelectedEntity() == ent);
      if (ImGui::Selectable(item.label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
        debugReg.SelectEntity(ent);
      }

      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(item.posStr.c_str());

      ImGui::TableSetColumnIndex(2);
      ImGui::TextUnformatted(item.sizeStr.c_str());

      ImGui::TableSetColumnIndex(3);
      ImGui::Text("%d", item.z);
    }
    ImGui::EndTable();
  }
}

}  // namespace

void DrawUIPanel(Registry& registry) {
  auto* debugReg = registry.TryGet<DebugMenuRegistry>();
  if (debugReg == nullptr) return;

  DrawOverlayControls(*debugReg);
  ImGui::Separator();
  DrawMountedViewsSection(registry);
  ImGui::Separator();
  DrawUIElementsSection(registry, *debugReg);
}

}  // namespace octarine::debug

#endif  // OCTARINE_WITH_IMGUI
