#pragma once

#include <sol/sol.hpp>

#include "Components/SeparationComponent.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<SeparationComponent> {
  static constexpr const char* kLuaKey = "separation";
  static constexpr const char* kUsertypeName = "separation_component";

  static SeparationComponent fromLua(const sol::object& data) {
    const auto t = data.as<sol::table>();
    SeparationComponent comp;
    comp.radius = t.get_or("radius", 40.0f);
    comp.weight = t.get_or("weight", 1.5f);
    return comp;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<SeparationComponent>(kUsertypeName, "radius", &SeparationComponent::radius, "weight",
                                          &SeparationComponent::weight);
  }
};
