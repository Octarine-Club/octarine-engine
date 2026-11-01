#pragma once

#include <sol/sol.hpp>

#include "Components/UIScrollComponent.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<UIScrollComponent> {
  static constexpr const char* kLuaKey = "ui_scroll";
  static constexpr const char* kUsertypeName = "ui_scroll_component";

  static UIScrollComponent fromLua(const sol::object& data) {
    UIScrollComponent scroll;
    if (!data.is<sol::table>()) return scroll;
    const auto t = data.as<sol::table>();
    using namespace LuaComponentHelpers;
    scroll.scrollX = SafeGetOptionalValue<float>(t, "scroll_x", 0.0f);
    scroll.scrollY = SafeGetOptionalValue<float>(t, "scroll_y", 0.0f);
    scroll.wheelStep = SafeGetOptionalValue<float>(t, "wheel_step", 36.0f);
    scroll.scrollHorizontal = SafeGetOptionalValue<bool>(t, "horizontal", false);
    scroll.scrollVertical = SafeGetOptionalValue<bool>(t, "vertical", true);
    return scroll;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<UIScrollComponent>(
        kUsertypeName, "scroll_x", &UIScrollComponent::scrollX, "scroll_y", &UIScrollComponent::scrollY,
        "content_width", &UIScrollComponent::contentWidth, "content_height", &UIScrollComponent::contentHeight,
        "wheel_step", &UIScrollComponent::wheelStep, "horizontal", &UIScrollComponent::scrollHorizontal, "vertical",
        &UIScrollComponent::scrollVertical);
  }
};
