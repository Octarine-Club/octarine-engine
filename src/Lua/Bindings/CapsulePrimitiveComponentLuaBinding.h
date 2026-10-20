#pragma once

#include <sol/sol.hpp>
#include <string>
#include <string_view>

#include "Components/CapsulePrimitiveComponent.h"
#include "General/BlendMode.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<CapsulePrimitiveComponent> {
  static constexpr const char* kLuaKey = "capsule";
  static constexpr const char* kUsertypeName = "capsule_primitive_component";

  static CapsulePrimitiveComponent fromLua(const sol::object& data) {
    const auto t = data.as<sol::table>();
    using namespace LuaComponentHelpers;
    const glm::vec2 start = SafeGetVec2(t, "start");
    const glm::vec2 end = SafeGetVec2(t, "end");
    const auto layer = SafeGetOptionalValue<int>(t, "layer", 1);
    const auto radius = SafeGetOptionalValue<float>(t, "radius", 0.0f);
    const auto thickness = SafeGetOptionalValue<float>(t, "thickness", 1.0f);
    const octarine::Color color = SafeGetColor(t, "color");
    const auto fixed = SafeGetOptionalValue<bool>(t, "fixed", false);
    const auto filled = SafeGetOptionalValue<bool>(t, "filled", true);
    CapsulePrimitiveComponent cap(start, end, layer, radius, color, fixed, filled, thickness);
    cap.blendMode = octarine::BlendModeFromString(SafeGetOptionalValue<std::string>(t, "blend_mode", "blend"));
    return cap;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<CapsulePrimitiveComponent>(
        kUsertypeName, "start", &CapsulePrimitiveComponent::start, "end", &CapsulePrimitiveComponent::end, "layer",
        &CapsulePrimitiveComponent::layer, "radius", &CapsulePrimitiveComponent::radius, "thickness",
        &CapsulePrimitiveComponent::thickness, "color", &CapsulePrimitiveComponent::color, "is_fixed",
        &CapsulePrimitiveComponent::isFixed, "filled", &CapsulePrimitiveComponent::filled, "blend_mode",
        sol::property([](const CapsulePrimitiveComponent& cp) { return octarine::ToString(cp.blendMode); },
                      [](CapsulePrimitiveComponent& cp, const std::string_view mode) {
                        cp.blendMode = octarine::BlendModeFromString(mode, cp.blendMode);
                      }));
  }
};
