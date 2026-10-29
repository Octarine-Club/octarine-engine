#pragma once

#include <sol/sol.hpp>
#include <string>

#include "Components/UIFlexComponent.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<UIFlexComponent> {
  static constexpr const char* kLuaKey = "ui_flex";
  static constexpr const char* kUsertypeName = "ui_flex_component";

  static FlexDirection ParseDirection(const std::string& s) {
    if (s == "column" || s == "col" || s == "vertical") return FlexDirection::Column;
    return FlexDirection::Row;
  }

  static FlexAlign ParseAlign(const std::string& s) {
    if (s == "start" || s == "left" || s == "top") return FlexAlign::Start;
    if (s == "center" || s == "middle") return FlexAlign::Center;
    if (s == "end" || s == "right" || s == "bottom") return FlexAlign::End;
    if (s == "space_between" || s == "space-between") return FlexAlign::SpaceBetween;
    if (s == "space_around" || s == "space-around") return FlexAlign::SpaceAround;
    return FlexAlign::Stretch;
  }

  static UIFlexComponent fromLua(const sol::object& data) {
    const auto t = data.as<sol::table>();
    using namespace LuaComponentHelpers;
    UIFlexComponent flex;
    flex.direction = ParseDirection(SafeGetOptionalValue<std::string>(t, "direction", "row"));
    flex.gap = SafeGetOptionalValue<float>(t, "gap", 0.0f);
    const auto pad = SafeGetOptionalValue<float>(t, "padding", 0.0f);
    flex.paddingLeft = SafeGetOptionalValue<float>(t, "padding_left", pad);
    flex.paddingTop = SafeGetOptionalValue<float>(t, "padding_top", pad);
    flex.paddingRight = SafeGetOptionalValue<float>(t, "padding_right", pad);
    flex.paddingBottom = SafeGetOptionalValue<float>(t, "padding_bottom", pad);
    flex.align = ParseAlign(SafeGetOptionalValue<std::string>(t, "align", "stretch"));
    flex.justify = ParseAlign(SafeGetOptionalValue<std::string>(t, "justify", "stretch"));
    return flex;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<UIFlexComponent>(
        kUsertypeName, "gap", &UIFlexComponent::gap, "padding_left", &UIFlexComponent::paddingLeft, "padding_top",
        &UIFlexComponent::paddingTop, "padding_right", &UIFlexComponent::paddingRight, "padding_bottom",
        &UIFlexComponent::paddingBottom, "direction",
        sol::property([](const UIFlexComponent& f) { return f.direction == FlexDirection::Column ? "column" : "row"; },
                      [](UIFlexComponent& f, const std::string& s) { f.direction = ParseDirection(s); }));
  }
};
