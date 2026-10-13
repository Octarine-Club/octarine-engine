#pragma once

#include <sol/sol.hpp>

#include "Components/HomingComponent.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<HomingComponent> {
  static constexpr const char* kLuaKey = "homing";
  static constexpr const char* kUsertypeName = "homing_component";

  static HomingComponent fromLua(const sol::object& data) {
    const auto t = data.as<sol::table>();
    HomingComponent comp;
    if (t["target"].valid()) {
      comp.target = t.get<Entity>("target");
    }
    comp.speed = t.get_or("speed", 600.0f);
    comp.retarget_on_hit = t.get_or("retarget_on_hit", false);
    return comp;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<HomingComponent>(kUsertypeName, "target", &HomingComponent::target, "speed",
                                      &HomingComponent::speed, "retarget_on_hit", &HomingComponent::retarget_on_hit);
  }
};
