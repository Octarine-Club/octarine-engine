#pragma once

#ifdef OCTARINE_WITH_EDITOR

#include <imgui.h>

#include <optional>

#include "Components/CapsuleColliderComponent.h"
#include "Editor/Inspectors/EditorInspector.h"

template <>
struct EditorInspector<CapsuleColliderComponent> {
  static constexpr const char* kDisplayName = "CapsuleCollider";
  static void draw(Registry* /*registry*/, Entity /*entity*/, CapsuleColliderComponent& cc) {
    ImGui::DragFloat2("Start", &cc.start.x, 1.0F);
    ImGui::DragFloat2("End", &cc.end.x, 1.0F);
    ImGui::DragFloat("Radius", &cc.radius, 1.0F);
    ImGui::Checkbox("Fixed", &cc.isFixed);
  }
  static std::optional<CapsuleColliderComponent> makeDefault() { return CapsuleColliderComponent{}; }
};

#endif  // OCTARINE_WITH_EDITOR
