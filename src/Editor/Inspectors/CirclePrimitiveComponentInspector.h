#pragma once

#ifdef OCTARINE_WITH_EDITOR

#include <imgui.h>

#include <optional>

#include "Components/CirclePrimitiveComponent.h"
#include "Editor/Inspectors/EditorInspector.h"
#include "Editor/Inspectors/InspectorWidgets.h"

template <>
struct EditorInspector<CirclePrimitiveComponent> {
  static constexpr const char* kDisplayName = "CirclePrimitive";
  static void draw(Registry* /*registry*/, Entity /*entity*/, CirclePrimitiveComponent& cp) {
    ImGui::DragFloat2("Position", &cp.position.x);
    ImGui::DragInt("Layer", &cp.layer);
    ImGui::DragFloat("Radius", &cp.radius);
    ImGui::DragFloat("Thickness", &cp.thickness);
    octarine::editor::inspectors::DrawColorEdit("Color", cp.color);
    ImGui::Checkbox("Fixed", &cp.isFixed);
    ImGui::Checkbox("Filled", &cp.filled);
  }
  static std::optional<CirclePrimitiveComponent> makeDefault() { return CirclePrimitiveComponent{}; }
};

#endif  // OCTARINE_WITH_EDITOR
