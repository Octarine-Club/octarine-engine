#pragma once

#include <sol/sol.hpp>

#include "Components/WeaponZoneComponent.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<WeaponZoneComponent> {
  static constexpr const char* kLuaKey = "weapon_zone";
  static constexpr const char* kUsertypeName = "weapon_zone_component";

  static WeaponZoneComponent fromLua(const sol::object& data) {
    const auto t = data.as<sol::table>();
    WeaponZoneComponent comp;
    comp.damage = t.get_or("damage", 1.0f);
    comp.pierce = t.get_or("pierce", 1);
    comp.rehit_cooldown = t.get_or("rehit_cooldown", 0.25f);
    comp.is_dash_weapon = t.get_or("is_dash", false);
    return comp;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<WeaponZoneComponent>(
        kUsertypeName, "damage", &WeaponZoneComponent::damage, "pierce", &WeaponZoneComponent::pierce, "rehit_cooldown",
        &WeaponZoneComponent::rehit_cooldown, "is_dash", &WeaponZoneComponent::is_dash_weapon, "consumed",
        &WeaponZoneComponent::consumed);
  }
};
