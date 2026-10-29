#pragma once

#ifdef OCTARINE_WITH_EDITOR

#include <imgui.h>

#include <optional>

#include "Components/UIButtonComponent.h"
#include "Editor/Inspectors/EditorInspector.h"
#include "Editor/Inspectors/InspectorWidgets.h"

template <>
struct EditorInspector<UIButtonComponent> {
  static constexpr const char* kDisplayName = "UIButton";
  static void draw(Registry* /*registry*/, Entity /*entity*/, UIButtonComponent& ub) {
    ImGui::Checkbox("Active", &ub.isActive);
    ImGui::Checkbox("Fixed", &ub.isFixed);
    octarine::editor::inspectors::InputTextString("Key", ub.key);
    octarine::editor::inspectors::InputTextString("Controller Button", ub.controllerButton);
    octarine::editor::inspectors::InputTextString("Action", ub.action);
    ImGui::Checkbox("Use Hover Color", &ub.hasHoverColor);
    if (ub.hasHoverColor) {
      octarine::editor::inspectors::DrawColorEdit("Hover Color", ub.hoverColor);
    }
    ImGui::Checkbox("Use Press Color", &ub.hasPressColor);
    if (ub.hasPressColor) {
      octarine::editor::inspectors::DrawColorEdit("Press Color", ub.pressColor);
    }
    ImGui::Text("Hovered: %s | Pressed: %s", ub.isHovered ? "yes" : "no", ub.isPressed ? "yes" : "no");
  }
  static std::optional<UIButtonComponent> makeDefault() { return UIButtonComponent{}; }
};

#endif  // OCTARINE_WITH_EDITOR
