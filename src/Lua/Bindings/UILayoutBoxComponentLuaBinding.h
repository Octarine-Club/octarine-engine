#pragma once

#include <cstdlib>
#include <sol/sol.hpp>
#include <string>
#include <string_view>
#include <utility>

#include "Components/UILayoutBoxComponent.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<UILayoutBoxComponent> {
  static constexpr const char* kLuaKey = "ui_layout_box";
  static constexpr const char* kUsertypeName = "ui_layout_box_component";

  static std::pair<float, UIUnit> ParseLength(const sol::object& obj) {
    if (!obj.valid() || obj == sol::lua_nil) return {0.0f, UIUnit::Auto};
    if (obj.get_type() == sol::type::number) {
      return {obj.as<float>(), UIUnit::Px};
    }
    if (obj.get_type() != sol::type::string) return {0.0f, UIUnit::Auto};
    const std::string s = obj.as<std::string>();
    if (s.empty() || s == "auto") return {0.0f, UIUnit::Auto};
    char* endPtr = nullptr;
    const float val = std::strtof(s.c_str(), &endPtr);
    if (endPtr == s.c_str()) return {0.0f, UIUnit::Auto};
    const std::string_view suffix(endPtr);
    if (suffix == "%") return {val, UIUnit::Percent};
    if (suffix == "fr") return {val, UIUnit::Fr};
    return {val, UIUnit::Px};
  }

  static void ApplyDim(const sol::table& t, const char* key, float& outVal, UIUnit& outUnit) {
    const sol::object obj = t[key];
    if (!obj.valid() || obj == sol::lua_nil) return;
    const auto [val, unit] = ParseLength(obj);
    outVal = val;
    outUnit = unit;
  }

  static UILayoutBoxComponent fromLua(const sol::object& data) {
    UILayoutBoxComponent box;
    if (!data.is<sol::table>()) return box;
    const auto t = data.as<sol::table>();
    using namespace LuaComponentHelpers;
    ApplyDim(t, "width", box.width, box.widthUnit);
    ApplyDim(t, "height", box.height, box.heightUnit);
    ApplyDim(t, "min_width", box.minWidth, box.minWidthUnit);
    ApplyDim(t, "max_width", box.maxWidth, box.maxWidthUnit);
    ApplyDim(t, "min_height", box.minHeight, box.minHeightUnit);
    ApplyDim(t, "max_height", box.maxHeight, box.maxHeightUnit);

    const float margin = SafeGetOptionalValue<float>(t, "margin", 0.0f);
    box.marginLeft = SafeGetOptionalValue<float>(t, "margin_left", margin);
    box.marginTop = SafeGetOptionalValue<float>(t, "margin_top", margin);
    box.marginRight = SafeGetOptionalValue<float>(t, "margin_right", margin);
    box.marginBottom = SafeGetOptionalValue<float>(t, "margin_bottom", margin);
    box.visible = SafeGetOptionalValue<bool>(t, "visible", true);
    return box;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<UILayoutBoxComponent>(
        kUsertypeName, "width", &UILayoutBoxComponent::width, "height", &UILayoutBoxComponent::height, "min_width",
        &UILayoutBoxComponent::minWidth, "max_width", &UILayoutBoxComponent::maxWidth, "min_height",
        &UILayoutBoxComponent::minHeight, "max_height", &UILayoutBoxComponent::maxHeight, "margin_left",
        &UILayoutBoxComponent::marginLeft, "margin_top", &UILayoutBoxComponent::marginTop, "margin_right",
        &UILayoutBoxComponent::marginRight, "margin_bottom", &UILayoutBoxComponent::marginBottom, "visible",
        &UILayoutBoxComponent::visible);
  }
};
