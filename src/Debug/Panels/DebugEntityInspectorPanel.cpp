#include "Debug/Panels/DebugEntityInspectorPanel.h"

#ifdef OCTARINE_WITH_IMGUI
#include <algorithm>
#include <string>
#include <vector>

#include "Components/NameComponent.h"
#include "Components/ScriptComponent.h"
#include "Debug/DebugComponentInspectors.h"
#include "Debug/DebugMenuRegistry.h"
#include "ECS/Entity.h"
#include "ECS/Registry.h"
#include "ECS/StateInspector.h"
#include "imgui.h"

namespace octarine::debug {

namespace {

std::string GetEntityDisplayName(const Registry& registry, Entity ent) {
  std::string name = "Entity " + std::to_string(ent.GetId());
  if (registry.HasComponent<NameComponent>(ent)) {
    const auto& nc = registry.GetComponent<NameComponent>(ent);
    if (!nc.name.empty()) {
      name += " (" + nc.name + ")";
    }
  }
  return name;
}

bool PassesFilter(const Registry& registry, Entity ent, const std::string& filter) {
  if (filter.empty()) return true;

  if (filter.starts_with("@")) {
    const std::string compFilter = filter.substr(1);
    const auto& all = DebugComponentInspectorRegistry::All();
    return std::ranges::any_of(all, [&](const auto& entry) {
      return entry.hasComponent(registry, ent) && entry.displayName.find(compFilter) != std::string::npos;
    });
  }

  const std::string name = GetEntityDisplayName(registry, ent);
  return name.find(filter) != std::string::npos;
}

void DrawEntityListToolbar(Registry& registry, DebugMenuRegistry& debugReg) {
  if (ImGui::Button("+ Create")) {
    const Entity created = registry.CreateEntity();
    debugReg.SelectEntity(created);
  }
  ImGui::SameLine();
  const Entity selected = debugReg.GetSelectedEntity();
  const bool hasSelected = registry.IsAlive(selected);
  ImGui::BeginDisabled(!hasSelected);
  if (ImGui::Button("Destroy")) {
    registry.BlamEntity(selected);
    debugReg.SelectEntity(Entity{});
  }
  ImGui::EndDisabled();

  ImGui::SameLine();
  bool picker = debugReg.IsWorldPickerEnabled();
  if (ImGui::Checkbox("Pick (Alt+Click)", &picker)) {
    debugReg.SetWorldPickerEnabled(picker);
  }
}

void DrawEntityList(Registry& registry, DebugMenuRegistry& debugReg, const std::string& filter) {
  const auto entities = registry.GetUserEntities();
  DrawEntityListToolbar(registry, debugReg);
  ImGui::Separator();

  if (ImGui::BeginChild("##entity_list", ImVec2(0, 0), true)) {
    std::vector<Entity> displayEntities;
    displayEntities.reserve(entities.size());
    for (const auto ent : entities) {
      if (PassesFilter(registry, ent, filter)) {
        displayEntities.push_back(ent);
      }
    }

    const Entity selected = debugReg.GetSelectedEntity();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(displayEntities.size()));
    while (clipper.Step()) {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
        const Entity ent = displayEntities[static_cast<size_t>(i)];
        const std::string label = GetEntityDisplayName(registry, ent);
        const bool isSelected = (ent == selected);
        if (ImGui::Selectable(label.c_str(), isSelected)) {
          debugReg.SelectEntity(ent);
        }
      }
    }
  }
  ImGui::EndChild();
}

void DrawAddComponentMenu(Registry& registry, Entity selected) {
  if (ImGui::BeginCombo("##add_comp", "Add Component...")) {
    for (const auto& entry : DebugComponentInspectorRegistry::All()) {
      if (!entry.hasComponent(registry, selected) && entry.addDefault) {
        if (ImGui::Selectable(entry.displayName.c_str())) {
          entry.addDefault(registry, selected);
        }
      }
    }
    ImGui::EndCombo();
  }
}

void DrawComponentHeaders(Registry& registry, Entity selected) {
  for (const auto& entry : DebugComponentInspectorRegistry::All()) {
    if (!entry.hasComponent(registry, selected)) continue;

    ImGui::PushID(static_cast<int>(entry.componentId));
    const bool open = ImGui::CollapsingHeader(entry.displayName.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

    if (entry.removeComponent) {
      ImGui::SameLine(ImGui::GetWindowWidth() - 75.0f);
      if (ImGui::SmallButton("Remove")) {
        entry.removeComponent(registry, selected);
        ImGui::PopID();
        continue;
      }
    }

    if (open) {
      ImGui::Indent();
      entry.drawInspector(registry, selected);
      ImGui::Unindent();
    }
    ImGui::PopID();
  }
}

struct JsonDumpCache {
  Entity lastEntity{};
  float timer = 0.0f;
  std::string cachedJson{};
};

JsonDumpCache& GetJsonCache() {
  static JsonDumpCache cache;
  return cache;
}

void DrawSelectedEntity(Registry& registry, Entity selected) {
  if (!registry.IsAlive(selected)) {
    ImGui::TextDisabled("No entity selected.\nSelect an entity from the list or Alt+Click in the viewport.");
    return;
  }

  bool active = registry.IsActive(selected);
  if (ImGui::Checkbox("Active", &active)) {
    if (active)
      registry.Activate(selected);
    else
      registry.Deactivate(selected);
  }
  ImGui::SameLine();
  ImGui::Text("ID: %llu", static_cast<unsigned long long>(selected.GetId()));

  ImGui::Separator();
  DrawAddComponentMenu(registry, selected);
  ImGui::Separator();

  if (ImGui::BeginChild("##components_view")) {
    DrawComponentHeaders(registry, selected);

    if (registry.HasComponent<ScriptComponent>(selected)) {
      if (ImGui::CollapsingHeader("ScriptComponent (Lua State)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        DrawScriptComponentLuaTableInspector(registry, selected);
        ImGui::Unindent();
      }
    }

    if (ImGui::CollapsingHeader("Raw State JSON (StateInspector)")) {
      auto& cache = GetJsonCache();
      const float dt = ImGui::GetIO().DeltaTime;
      cache.timer -= dt;

      if (cache.lastEntity != selected || cache.timer <= 0.0f) {
        cache.lastEntity = selected;
        cache.timer = 0.25f;  // Refresh 4 times per second
        cache.cachedJson = octarine::inspect::StateInspector::DumpEntity(registry, selected);
      }

      if (ImGui::SmallButton("Copy JSON")) {
        ImGui::SetClipboardText(cache.cachedJson.c_str());
      }
      ImGui::Separator();
      ImGui::TextUnformatted(cache.cachedJson.c_str());
    }
  }
  ImGui::EndChild();
}

}  // namespace

void DrawEntityInspectorPanel(Registry& registry) {
  auto* debugReg = registry.TryGet<DebugMenuRegistry>();
  if (debugReg == nullptr) return;

  static char filterBuf[128] = "";
  ImGui::InputTextWithHint("##filter", "Filter (name or @Component)...", filterBuf, sizeof(filterBuf));

  ImGui::Columns(2, "##inspector_columns", true);

  DrawEntityList(registry, *debugReg, filterBuf);

  ImGui::NextColumn();

  DrawSelectedEntity(registry, debugReg->GetSelectedEntity());

  ImGui::Columns(1);
}

}  // namespace octarine::debug

#endif  // OCTARINE_WITH_IMGUI
