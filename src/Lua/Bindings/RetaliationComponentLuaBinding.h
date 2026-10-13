#pragma once

#include <sol/sol.hpp>

#include "Components/RetaliationComponent.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<RetaliationComponent> {
  static constexpr const char* kLuaKey = "retaliation";
  static constexpr const char* kUsertypeName = "retaliation_component";

  static RetaliationComponent fromLua(const sol::object& data) {
    const auto t = data.as<sol::table>();
    using namespace LuaComponentHelpers;
    RetaliationComponent comp;
    comp.target_entity = LuaComponentHelpers::SafeGetOptionalValue<Entity>(t, "target_entity", Entity{});
    comp.size = SafeGetOptionalValue<float>(t, "size", 80.0f);
    comp.damage = SafeGetOptionalValue<float>(t, "damage", 15.0f);
    comp.lifetime = SafeGetOptionalValue<float>(t, "lifetime", 0.5f);
    comp.tick_interval = SafeGetOptionalValue<float>(t, "tick_interval", 0.1f);
    if (t["color"].valid()) {
      comp.color = SafeGetColor(t, "color");
    }
    return comp;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<RetaliationComponent>(kUsertypeName, "target_entity", &RetaliationComponent::target_entity, "size",
                                           &RetaliationComponent::size, "damage", &RetaliationComponent::damage,
                                           "lifetime", &RetaliationComponent::lifetime, "tick_interval",
                                           &RetaliationComponent::tick_interval, "color", &RetaliationComponent::color);
  }
};
