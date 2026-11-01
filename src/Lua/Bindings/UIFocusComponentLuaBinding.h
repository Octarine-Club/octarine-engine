#pragma once

#include <sol/sol.hpp>

#include "Components/UIFocusComponent.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<UIFocusComponent> {
  static constexpr const char* kLuaKey = "ui_focus";
  static constexpr const char* kUsertypeName = "ui_focus_component";

  static UIFocusComponent fromLua(const sol::object& data) {
    UIFocusComponent foc;
    if (data.is<bool>()) {
      foc.isFocusable = data.as<bool>();
      return foc;
    }
    if (!data.is<sol::table>()) return foc;
    const auto t = data.as<sol::table>();
    using namespace LuaComponentHelpers;
    foc.tabIndex = SafeGetOptionalValue<int>(t, "tab_index", 0);
    foc.isFocused = SafeGetOptionalValue<bool>(t, "is_focused", false);
    foc.isFocusable = SafeGetOptionalValue<bool>(t, "focusable", true);
    foc.isChecked = SafeGetOptionalValue<bool>(t, "checked", false);
    foc.isDisabled = SafeGetOptionalValue<bool>(t, "disabled", false);
    if (t["nav_up"].is<Entity>()) foc.navUp = t["nav_up"].get<Entity>();
    if (t["nav_down"].is<Entity>()) foc.navDown = t["nav_down"].get<Entity>();
    if (t["nav_left"].is<Entity>()) foc.navLeft = t["nav_left"].get<Entity>();
    if (t["nav_right"].is<Entity>()) foc.navRight = t["nav_right"].get<Entity>();
    return foc;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<UIFocusComponent>(
        kUsertypeName, "tab_index", &UIFocusComponent::tabIndex, "is_focused", &UIFocusComponent::isFocused,
        "focusable", &UIFocusComponent::isFocusable, "checked", &UIFocusComponent::isChecked, "disabled",
        &UIFocusComponent::isDisabled, "nav_up", &UIFocusComponent::navUp, "nav_down", &UIFocusComponent::navDown,
        "nav_left", &UIFocusComponent::navLeft, "nav_right", &UIFocusComponent::navRight);
  }
};
