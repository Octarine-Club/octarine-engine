#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ECS/Component.h"
#include "ECS/Entity.h"
#include "ECS/Registry.h"

namespace octarine::debug {

struct ComponentInspectorEntry {
  uint32_t componentId = 0;
  std::string displayName{};
  std::function<bool(const Registry&, Entity)> hasComponent{};
  std::function<void(Registry&, Entity)> drawInspector{};
  std::function<void(Registry&, Entity)> addDefault{};
  std::function<void(Registry&, Entity)> removeComponent{};
};

class DebugComponentInspectorRegistry {
 public:
  template <typename T>
  static void Register(std::string displayName, std::function<void(Registry&, Entity, T&)> drawer,
                       bool allowAddRemove = true) {
    auto& entries = MutableAll();
    const uint32_t cid = GetComponentFamily<T>();
    for (auto& entry : entries) {
      if (entry.componentId == cid) {
        entry.displayName = std::move(displayName);
        entry.drawInspector = [drawer](Registry& reg, Entity ent) {
          if (reg.HasComponent<T>(ent)) {
            drawer(reg, ent, reg.GetComponent<T>(ent));
          }
        };
        return;
      }
    }

    ComponentInspectorEntry entry;
    entry.componentId = cid;
    entry.displayName = std::move(displayName);
    entry.hasComponent = [](const Registry& reg, Entity ent) { return reg.HasComponent<T>(ent); };
    entry.drawInspector = [drawer](Registry& reg, Entity ent) {
      if (reg.HasComponent<T>(ent)) {
        drawer(reg, ent, reg.GetComponent<T>(ent));
      }
    };
    if (allowAddRemove) {
      entry.addDefault = [](Registry& reg, Entity ent) {
        if (!reg.HasComponent<T>(ent)) {
          reg.AddComponent<T>(ent, T{});
        }
      };
      entry.removeComponent = [](Registry& reg, Entity ent) {
        if (reg.HasComponent<T>(ent)) {
          reg.RemoveComponent<T>(ent);
        }
      };
    }
    entries.push_back(std::move(entry));
  }

  static void InstallBuiltInComponentInspectors();
  static const std::vector<ComponentInspectorEntry>& All();

 private:
  static std::vector<ComponentInspectorEntry>& MutableAll();
};

#ifdef OCTARINE_WITH_IMGUI
void DrawScriptComponentLuaTableInspector(Registry& registry, Entity entity);
#endif

}  // namespace octarine::debug
