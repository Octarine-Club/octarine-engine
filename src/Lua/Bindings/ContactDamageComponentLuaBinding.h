#pragma once

#include <sol/sol.hpp>

#include "Components/ContactDamageComponent.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<ContactDamageComponent> {
  static constexpr const char* kLuaKey = "contact_damage";
  static constexpr const char* kUsertypeName = "contact_damage_component";

  static ContactDamageComponent fromLua(const sol::object& data) {
    const auto t = data.as<sol::table>();
    ContactDamageComponent comp;
    comp.damage = t.get_or("damage", 10.0f);
    comp.cooldown = t.get_or("cooldown", 1.0f);
    comp.current_cooldown = t.get_or("current_cooldown", 0.0f);
    comp.target_entity = LuaComponentHelpers::SafeGetOptionalValue<Entity>(t, "target_entity", Entity{});
    return comp;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<ContactDamageComponent>(kUsertypeName, "damage", &ContactDamageComponent::damage, "cooldown",
                                             &ContactDamageComponent::cooldown, "current_cooldown",
                                             &ContactDamageComponent::current_cooldown, "target_entity",
                                             &ContactDamageComponent::target_entity);
  }
};
