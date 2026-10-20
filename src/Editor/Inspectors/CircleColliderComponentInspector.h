#pragma once

#ifdef OCTARINE_WITH_EDITOR

#include <imgui.h>

#include <optional>

#include "Components/CircleColliderComponent.h"
#include "Editor/Inspectors/EditorInspector.h"

template <>
struct EditorInspector<CircleColliderComponent> {
  static constexpr const char* kDisplayName = "CircleCollider";
  static void draw(Registry* /*registry*/, Entity /*entity*/, CircleColliderComponent& cc) {
    ImGui::DragFloat("Radius", &cc.radius, 1.0F);
    ImGui::DragFloat2("Offset", &cc.offset.x, 1.0F);
    ImGui::Checkbox("Fixed", &cc.isFixed);
  }
  static std::optional<CircleColliderComponent> makeDefault() { return CircleColliderComponent{}; }
};

#endif  // OCTARINE_WITH_EDITOR
