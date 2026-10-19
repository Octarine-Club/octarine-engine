#pragma once

#ifdef OCTARINE_WITH_EDITOR

#include <imgui.h>

#include <optional>

#include "Components/TriangleColliderComponent.h"
#include "Editor/Inspectors/EditorInspector.h"

template <>
struct EditorInspector<TriangleColliderComponent> {
  static constexpr const char* kDisplayName = "TriangleCollider";
  static void draw(Registry* /*registry*/, Entity /*entity*/, TriangleColliderComponent& tc) {
    ImGui::DragFloat2("P1", &tc.p1.x, 1.0F);
    ImGui::DragFloat2("P2", &tc.p2.x, 1.0F);
    ImGui::DragFloat2("P3", &tc.p3.x, 1.0F);
    ImGui::Checkbox("Fixed", &tc.isFixed);
  }
  static std::optional<TriangleColliderComponent> makeDefault() { return TriangleColliderComponent{}; }
};

#endif  // OCTARINE_WITH_EDITOR
