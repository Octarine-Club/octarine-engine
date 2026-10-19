#pragma once

#ifdef OCTARINE_WITH_EDITOR

#include <imgui.h>

#include <optional>

#include "Components/TrianglePrimitiveComponent.h"
#include "Editor/Inspectors/EditorInspector.h"
#include "Editor/Inspectors/InspectorWidgets.h"

template <>
struct EditorInspector<TrianglePrimitiveComponent> {
  static constexpr const char* kDisplayName = "TrianglePrimitive";
  static void draw(Registry* /*registry*/, Entity /*entity*/, TrianglePrimitiveComponent& tp) {
    ImGui::DragFloat2("P1", &tp.p1.x);
    ImGui::DragFloat2("P2", &tp.p2.x);
    ImGui::DragFloat2("P3", &tp.p3.x);
    ImGui::DragInt("Layer", &tp.layer);
    ImGui::DragFloat("Thickness", &tp.thickness);
    octarine::editor::inspectors::DrawColorEdit("Color", tp.color);
    ImGui::Checkbox("Fixed", &tp.isFixed);
    ImGui::Checkbox("Filled", &tp.filled);
  }
  static std::optional<TrianglePrimitiveComponent> makeDefault() { return TrianglePrimitiveComponent{}; }
};

#endif  // OCTARINE_WITH_EDITOR
