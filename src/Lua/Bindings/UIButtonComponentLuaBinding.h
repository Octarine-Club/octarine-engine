#pragma once

#include <sol/sol.hpp>
#include <string>
#include <utility>

#include "Components/UIButtonComponent.h"
#include "ECS/Entity.h"
#include "General/Logger.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<UIButtonComponent> {
  static constexpr const char* kLuaKey = "ui_button";
  static constexpr const char* kUsertypeName = "ui_button_component";

  static UIButtonComponent fromLua(const sol::object& data) {
    const auto t = data.as<sol::table>();
    using namespace LuaComponentHelpers;
    const bool isActive = SafeGetOptionalValue<bool>(t, "is_active", true);
    const bool isFixed = SafeGetOptionalValue<bool>(t, "is_fixed", true);
    const sol::protected_function clickFn = SafeGetProtectedFunction(t, "on_click");
    const sol::protected_function focusFn = SafeGetProtectedFunction(t, "on_focus");

    auto key = SafeGetOptionalValue<std::string>(t, "key", "");
    if (key.empty()) {
      key = SafeGetOptionalValue<std::string>(t, "input_key", "");
    }
    auto controllerButton = SafeGetOptionalValue<std::string>(t, "gamepad", "");
    if (controllerButton.empty()) {
      controllerButton = SafeGetOptionalValue<std::string>(t, "gamepad_button", "");
    }
    if (controllerButton.empty()) {
      controllerButton = SafeGetOptionalValue<std::string>(t, "controller_button", "");
    }
    auto action = SafeGetOptionalValue<std::string>(t, "action", "");

    UIButtonComponent btn(isActive, isFixed, t, clickFn, std::move(key), std::move(controllerButton),
                          std::move(action));
    btn.focusFunction = focusFn;
    btn.hoverSound = SafeGetOptionalValue<std::string>(t, "hover_sound", "");
    btn.clickSound = SafeGetOptionalValue<std::string>(t, "click_sound", "");
    btn.focusSound = SafeGetOptionalValue<std::string>(t, "focus_sound", "");
    if (t["hover_color"].is<sol::table>()) {
      btn.hasHoverColor = true;
      btn.hoverColor = SafeGetColor(t, "hover_color", 255, 255, 255, 255);
    }
    if (t["press_color"].is<sol::table>()) {
      btn.hasPressColor = true;
      btn.pressColor = SafeGetColor(t, "press_color", 255, 255, 255, 255);
    }
    return btn;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<UIButtonComponent>(
        kUsertypeName, sol::no_constructor, "is_active", &UIButtonComponent::isActive, "is_fixed",
        &UIButtonComponent::isFixed, "is_hovered", &UIButtonComponent::isHovered, "is_pressed",
        &UIButtonComponent::isPressed, "has_hover_color", &UIButtonComponent::hasHoverColor, "has_press_color",
        &UIButtonComponent::hasPressColor, "hover_color", &UIButtonComponent::hoverColor, "press_color",
        &UIButtonComponent::pressColor, "key", &UIButtonComponent::key, "input_key", &UIButtonComponent::key, "gamepad",
        &UIButtonComponent::controllerButton, "controller_button", &UIButtonComponent::controllerButton,
        "gamepad_button", &UIButtonComponent::controllerButton, "action", &UIButtonComponent::action, "hover_sound",
        &UIButtonComponent::hoverSound, "click_sound", &UIButtonComponent::clickSound, "focus_sound",
        &UIButtonComponent::focusSound, "on_click", &UIButtonComponent::clickFunction, "on_focus",
        &UIButtonComponent::focusFunction, "trigger", [](UIButtonComponent& self, sol::optional<Entity> entity) {
          if (!self.isActive) return false;
          if (self.clickFunction != sol::lua_nil) {
            sol::protected_function clickFn = self.clickFunction;
            sol::object btnTable = self.buttonTable ? sol::object(*self.buttonTable) : sol::object(sol::lua_nil);
            auto result = entity ? clickFn(btnTable, *entity) : clickFn(btnTable);
            if (!result.valid()) {
              const sol::error err = result;
              Logger::ErrorLua(std::string(err.what()));
              return false;
            }
            return true;
          }
          return false;
        });
  }
};
