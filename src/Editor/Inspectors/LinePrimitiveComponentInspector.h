#pragma once

#ifdef OCTARINE_WITH_EDITOR

#include <imgui.h>

#include <optional>

#include "Components/LinePrimitiveComponent.h"
#include "Editor/Inspectors/EditorInspector.h"
#include "Editor/Inspectors/InspectorWidgets.h"

template <>
struct EditorInspector<LinePrimitiveComponent> {
  static constexpr const char* kDisplayName = "LinePrimitive";
  static void draw(Registry* /*registry*/, Entity /*entity*/, LinePrimitiveComponent& lp) {
    ImGui::DragFloat2("Start", &lp.start.x);
    ImGui::DragFloat2("End", &lp.end.x);
    ImGui::DragInt("Layer", &lp.layer);
    ImGui::DragFloat("Thickness", &lp.thickness);
    octarine::editor::inspectors::DrawColorEdit("Color", lp.color);
    ImGui::Checkbox("Fixed", &lp.isFixed);
  }
  static std::optional<LinePrimitiveComponent> makeDefault() { return LinePrimitiveComponent{}; }
};

#endif  // OCTARINE_WITH_EDITOR
