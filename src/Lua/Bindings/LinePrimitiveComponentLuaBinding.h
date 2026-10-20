#pragma once

#include <sol/sol.hpp>
#include <string>
#include <string_view>

#include "Components/LinePrimitiveComponent.h"
#include "General/BlendMode.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<LinePrimitiveComponent> {
  static constexpr const char* kLuaKey = "line";
  static constexpr const char* kUsertypeName = "line_primitive_component";

  static LinePrimitiveComponent fromLua(const sol::object& data) {
    const auto t = data.as<sol::table>();
    using namespace LuaComponentHelpers;
    const glm::vec2 start = SafeGetVec2(t, "start");
    const glm::vec2 end = SafeGetVec2(t, "end");
    const auto layer = SafeGetOptionalValue<int>(t, "layer", 1);
    const auto thickness = SafeGetOptionalValue<float>(t, "thickness", 1.0f);
    const octarine::Color color = SafeGetColor(t, "color");
    const auto fixed = SafeGetOptionalValue<bool>(t, "fixed", false);
    LinePrimitiveComponent line(start, end, layer, thickness, color, fixed);
    line.blendMode = octarine::BlendModeFromString(SafeGetOptionalValue<std::string>(t, "blend_mode", "blend"));
    return line;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<LinePrimitiveComponent>(
        kUsertypeName, "start", &LinePrimitiveComponent::start, "end", &LinePrimitiveComponent::end, "layer",
        &LinePrimitiveComponent::layer, "thickness", &LinePrimitiveComponent::thickness, "color",
        &LinePrimitiveComponent::color, "is_fixed", &LinePrimitiveComponent::isFixed, "blend_mode",
        sol::property([](const LinePrimitiveComponent& l) { return octarine::ToString(l.blendMode); },
                      [](LinePrimitiveComponent& l, const std::string_view mode) {
                        l.blendMode = octarine::BlendModeFromString(mode, l.blendMode);
                      }));
  }
};
