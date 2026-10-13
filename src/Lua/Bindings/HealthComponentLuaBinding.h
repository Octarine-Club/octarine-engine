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
    const auto t = data.as<sol::table>();
    using namespace LuaComponentHelpers;
    const int max_health = SafeGetOptionalValue<int>(t, "max_hp", SafeGetOptionalValue<int>(t, "max_health", 100));
    const int curHealth = SafeGetOptionalValue<int>(t, "hp", max_health);
    HealthComponent comp(max_health, curHealth);
    comp.dash_only = t.get_or("dash_only", false);
    sol::optional<sol::table> baseColor = t["base_color"];
    if (baseColor) {
      comp.base_color_r = baseColor.value().get_or("r", static_cast<unsigned char>(255));
      comp.base_color_g = baseColor.value().get_or("g", static_cast<unsigned char>(255));
      comp.base_color_b = baseColor.value().get_or("b", static_cast<unsigned char>(255));
      comp.base_color_a = baseColor.value().get_or("a", static_cast<unsigned char>(255));
    }
    return comp;
  }

  static void bindUsertype(sol::state& lua) {
    // current_health is a clamping property — direct writes from Lua are clamped to
    // [0, max_health] so scripts can't bypass damage()/heal() invariants by assigning the
    // field. Reads stay zero-overhead.
    lua.new_usertype<HealthComponent>(
        kUsertypeName, "current_health",
        sol::property([](const HealthComponent& h) { return h.current_health; },
                      [](HealthComponent& h, const int v) { h.current_health = std::clamp(v, 0, h.max_health); }),
        "max_health",
        sol::property([](const HealthComponent& h) { return h.max_health; },
                      [](HealthComponent& h, const int v) { h.max_health = v; }),
        "damage", &HealthComponent::Damage, "heal", &HealthComponent::Heal, "is_dead",
        sol::property(&HealthComponent::IsDead), "fraction", sol::property(&HealthComponent::Fraction));
  }
};
