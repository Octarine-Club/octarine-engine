#pragma once

#include <sol/sol.hpp>

#include "Components/MagnetComponent.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<MagnetComponent> {
  static constexpr const char* kLuaKey = "magnet";
  static constexpr const char* kUsertypeName = "magnet_component";

  static MagnetComponent fromLua(const sol::object& data) {
    const auto t = data.as<sol::table>();
    using namespace LuaComponentHelpers;
    MagnetComponent comp;
    comp.layer = SafeGetOptionalValue<uint32_t>(t, "layer", 0);
    comp.attracts_mask = SafeGetOptionalValue<uint32_t>(t, "attracts_mask", 0);
    comp.attraction_radius = SafeGetOptionalValue<float>(t, "attraction_radius", 120.0f);
    comp.speed = SafeGetOptionalValue<float>(t, "speed", 360.0f);
    comp.reference_entity = SafeGetOptionalValue<Entity>(t, "reference_entity", Entity{});
    return comp;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<MagnetComponent>(kUsertypeName, "layer", &MagnetComponent::layer, "attracts_mask",
                                      &MagnetComponent::attracts_mask, "attraction_radius",
                                      &MagnetComponent::attraction_radius, "speed", &MagnetComponent::speed,
                                      "reference_entity", &MagnetComponent::reference_entity);
  }
};
