#pragma once

#include <sol/sol.hpp>
#include <string>
#include <utility>

#include "General/Color.h"

struct UIButtonComponent {
  sol::optional<sol::table> buttonTable;
  sol::protected_function clickFunction;
  std::string key;
  std::string controllerButton;
  std::string action;
  std::string hoverSound{};
  std::string clickSound{};
  std::string focusSound{};
  bool isActive;
  bool isFixed;
  bool isHovered = false;
  bool isPressed = false;
  bool hasHoverColor = false;
  bool hasPressColor = false;
  octarine::Color hoverColor{0, 0, 0, 0};
  octarine::Color pressColor{0, 0, 0, 0};

  explicit UIButtonComponent(const bool t_isActive = true, const bool t_isFixed = true,
                             sol::optional<sol::table> t_buttonTable = sol::nullopt,
                             sol::protected_function t_clickFunction = sol::lua_nil, std::string t_key = "",
                             std::string t_controllerButton = "", std::string t_action = "")
      : buttonTable(std::move(t_buttonTable)),
        clickFunction(std::move(t_clickFunction)),
        key(std::move(t_key)),
        controllerButton(std::move(t_controllerButton)),
        action(std::move(t_action)),
        isActive(t_isActive),
        isFixed(t_isFixed) {}
};