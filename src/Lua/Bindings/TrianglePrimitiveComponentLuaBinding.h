#pragma once

#include <sol/sol.hpp>
#include <string>
#include <string_view>

#include "Components/TrianglePrimitiveComponent.h"
#include "General/BlendMode.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<TrianglePrimitiveComponent> {
  static constexpr const char* kLuaKey = "triangle";
  static constexpr const char* kUsertypeName = "triangle_primitive_component";

  static TrianglePrimitiveComponent fromLua(const sol::object& data) {
    const auto t = data.as<sol::table>();
    using namespace LuaComponentHelpers;
    const glm::vec2 p1 = SafeGetVec2(t, "p1");
    const glm::vec2 p2 = SafeGetVec2(t, "p2");
    const glm::vec2 p3 = SafeGetVec2(t, "p3");
    const auto layer = SafeGetOptionalValue<int>(t, "layer", 1);
    const auto thickness = SafeGetOptionalValue<float>(t, "thickness", 1.0f);
    const octarine::Color color = SafeGetColor(t, "color");
    const auto fixed = SafeGetOptionalValue<bool>(t, "fixed", false);
    const auto filled = SafeGetOptionalValue<bool>(t, "filled", true);
    TrianglePrimitiveComponent tri(p1, p2, p3, layer, color, fixed, filled, thickness);
    tri.blendMode = octarine::BlendModeFromString(SafeGetOptionalValue<std::string>(t, "blend_mode", "blend"));
    return tri;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<TrianglePrimitiveComponent>(
        kUsertypeName, "p1", &TrianglePrimitiveComponent::p1, "p2", &TrianglePrimitiveComponent::p2, "p3",
        &TrianglePrimitiveComponent::p3, "layer", &TrianglePrimitiveComponent::layer, "thickness",
        &TrianglePrimitiveComponent::thickness, "color", &TrianglePrimitiveComponent::color, "is_fixed",
        &TrianglePrimitiveComponent::isFixed, "filled", &TrianglePrimitiveComponent::filled, "blend_mode",
        sol::property([](const TrianglePrimitiveComponent& tr) { return octarine::ToString(tr.blendMode); },
                      [](TrianglePrimitiveComponent& tr, const std::string_view mode) {
                        tr.blendMode = octarine::BlendModeFromString(mode, tr.blendMode);
                      }));
  }
};
