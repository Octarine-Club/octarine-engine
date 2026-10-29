#pragma once

#ifdef OCTARINE_WITH_EDITOR

#include <imgui.h>

#include <optional>

#include "Components/SquarePrimitiveComponent.h"
#include "Editor/Inspectors/EditorInspector.h"
#include "Editor/Inspectors/InspectorWidgets.h"

template <>
struct EditorInspector<SquarePrimitiveComponent> {
  static constexpr const char* kDisplayName = "SquarePrimitive";
  static void draw(Registry* /*registry*/, Entity /*entity*/, SquarePrimitiveComponent& sp) {
    ImGui::DragFloat2("Position", &sp.position.x);
    ImGui::DragInt("Layer", &sp.layer);
    ImGui::DragFloat("Width", &sp.width);
    ImGui::DragFloat("Height", &sp.height);
    octarine::editor::inspectors::DrawColorEdit("Color", sp.color);
    ImGui::DragFloat("Border Thickness", &sp.borderThickness, 0.25f, 0.0f, 32.0f);
    if (sp.borderThickness > 0.0f) {
      octarine::editor::inspectors::DrawColorEdit("Border Color", sp.borderColor);
    }
    ImGui::DragFloat("Corner Radius", &sp.cornerRadius, 0.5f, 0.0f, 128.0f);
    ImGui::SliderFloat("Fill Ratio", &sp.fillRatio, 0.0f, 1.0f);
    ImGui::Checkbox("Fixed", &sp.isFixed);
  }
  static std::optional<SquarePrimitiveComponent> makeDefault() { return SquarePrimitiveComponent{}; }
};

#endif  // OCTARINE_WITH_EDITOR
