#pragma once

#ifdef OCTARINE_WITH_EDITOR

#include <imgui.h>

#include <optional>

#include "Components/LineColliderComponent.h"
#include "Editor/Inspectors/EditorInspector.h"

template <>
struct EditorInspector<LineColliderComponent> {
  static constexpr const char* kDisplayName = "LineCollider";
  static void draw(Registry* /*registry*/, Entity /*entity*/, LineColliderComponent& lc) {
    ImGui::DragFloat2("Start", &lc.start.x, 1.0F);
    ImGui::DragFloat2("End", &lc.end.x, 1.0F);
    ImGui::Checkbox("Fixed", &lc.isFixed);
  }
  static std::optional<LineColliderComponent> makeDefault() { return LineColliderComponent{}; }
};

#endif  // OCTARINE_WITH_EDITOR
