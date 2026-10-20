#pragma once

#include <sol/sol.hpp>
#include <string>
#include <string_view>

#include "Components/CirclePrimitiveComponent.h"
#include "General/BlendMode.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<CirclePrimitiveComponent> {
  static constexpr const char* kLuaKey = "circle";
  static constexpr const char* kUsertypeName = "circle_primitive_component";

  static CirclePrimitiveComponent fromLua(const sol::object& data) {
    const auto t = data.as<sol::table>();
    using namespace LuaComponentHelpers;
    const glm::vec2 position = SafeGetVec2(t, "position");
    const auto layer = SafeGetOptionalValue<int>(t, "layer", 1);
    const auto radius = SafeGetOptionalValue<float>(t, "radius", 0.0f);
    const auto thickness = SafeGetOptionalValue<float>(t, "thickness", 1.0f);
    const octarine::Color color = SafeGetColor(t, "color");
    const auto fixed = SafeGetOptionalValue<bool>(t, "fixed", false);
    const auto filled = SafeGetOptionalValue<bool>(t, "filled", true);
    CirclePrimitiveComponent circle(position, layer, radius, color, fixed, filled, thickness);
    circle.blendMode = octarine::BlendModeFromString(SafeGetOptionalValue<std::string>(t, "blend_mode", "blend"));
    return circle;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<CirclePrimitiveComponent>(
        kUsertypeName, "position", &CirclePrimitiveComponent::position, "layer", &CirclePrimitiveComponent::layer,
        "radius", &CirclePrimitiveComponent::radius, "thickness", &CirclePrimitiveComponent::thickness, "color",
        &CirclePrimitiveComponent::color, "is_fixed", &CirclePrimitiveComponent::isFixed, "filled",
        &CirclePrimitiveComponent::filled, "blend_mode",
        sol::property([](const CirclePrimitiveComponent& c) { return octarine::ToString(c.blendMode); },
                      [](CirclePrimitiveComponent& c, const std::string_view mode) {
                        c.blendMode = octarine::BlendModeFromString(mode, c.blendMode);
                      }));
  }
};
