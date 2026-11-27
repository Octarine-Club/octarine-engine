#pragma once

#include <algorithm>
#include <sol/sol.hpp>

#include "Components/HealthComponent.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<HealthComponent> {
  static constexpr const char* kLuaKey = "health";
  static constexpr const char* kUsertypeName = "health_component";

  static HealthComponent fromLua(const sol::object& data) {
    if (!data.is<sol::table>()) {
      return HealthComponent(100);
    }
    const auto t = data.as<sol::table>();
    using namespace LuaComponentHelpers;
    const int maxHealth = std::max(1, SafeGetOptionalValue<int>(t, "max_health", 100));
    const int regenPerSec = SafeGetOptionalValue<int>(t, "regen_per_second", 0);
    HealthComponent comp(maxHealth, regenPerSec);
    const int currentHealth = SafeGetOptionalValue<int>(t, "current_health", comp.max_health);
    comp.current_health = std::clamp(currentHealth, 0, comp.max_health);
    return comp;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<HealthComponent>(
        kUsertypeName, "max_health", &HealthComponent::max_health, "regen_per_second",
        &HealthComponent::regen_per_second, "current_health",
        sol::property([](const HealthComponent& h) { return h.current_health; },
                      [](HealthComponent& h, const int v) { h.current_health = std::clamp(v, 0, h.max_health); }),
        "is_dead", sol::property(&HealthComponent::IsDead), "damage", &HealthComponent::Damage, "heal",
        &HealthComponent::Heal);
  }
};
