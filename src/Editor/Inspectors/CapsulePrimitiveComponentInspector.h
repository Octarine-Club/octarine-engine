#pragma once

#ifdef OCTARINE_WITH_EDITOR

#include <imgui.h>

#include <optional>

#include "Components/CapsulePrimitiveComponent.h"
#include "Editor/Inspectors/EditorInspector.h"
#include "Editor/Inspectors/InspectorWidgets.h"

template <>
struct EditorInspector<CapsulePrimitiveComponent> {
  static constexpr const char* kDisplayName = "CapsulePrimitive";
  static void draw(Registry* /*registry*/, Entity /*entity*/, CapsulePrimitiveComponent& cp) {
    ImGui::DragFloat2("Start", &cp.start.x);
    ImGui::DragFloat2("End", &cp.end.x);
    ImGui::DragInt("Layer", &cp.layer);
    ImGui::DragFloat("Radius", &cp.radius);
    ImGui::DragFloat("Thickness", &cp.thickness);
    octarine::editor::inspectors::DrawColorEdit("Color", cp.color);
    ImGui::Checkbox("Fixed", &cp.isFixed);
    ImGui::Checkbox("Filled", &cp.filled);
  }
  static std::optional<CapsulePrimitiveComponent> makeDefault() { return CapsulePrimitiveComponent{}; }
};

#endif  // OCTARINE_WITH_EDITOR
