#pragma once

#include <sol/sol.hpp>

#include "Components/EnemySteeringComponent.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<EnemySteeringComponent> {
  static constexpr const char* kLuaKey = "enemy_steering";
  static constexpr const char* kUsertypeName = "enemy_steering_component";

  static EnemySteeringComponent fromLua(const sol::object& data) {
    const auto t = data.as<sol::table>();
    EnemySteeringComponent comp;

    auto typeStr = t.get_or<std::string>("type", "chase");
    if (typeStr == "chase")
      comp.type = EnemyBehaviorType::Chase;
    else if (typeStr == "charge")
      comp.type = EnemyBehaviorType::Charge;
    else if (typeStr == "skitter")
      comp.type = EnemyBehaviorType::Skitter;
    else if (typeStr == "orbit")
      comp.type = EnemyBehaviorType::Orbit;
    else if (typeStr == "flee")
      comp.type = EnemyBehaviorType::Flee;
    else if (typeStr == "chase_spitter")
      comp.type = EnemyBehaviorType::ChaseSpitter;
    else if (typeStr == "sine_flyer")
      comp.type = EnemyBehaviorType::SineFlyer;
    else if (typeStr == "fly_through")
      comp.type = EnemyBehaviorType::FlyThrough;
    else if (typeStr == "dummy")
      comp.type = EnemyBehaviorType::Dummy;

    comp.speed = t.get_or("speed", 100.0f);
    comp.half_size = t.get_or("half_size", t.get_or("halfSize", 16.0f));
    comp.charge_range = t.get_or("charge_range", 260.0f);
    comp.windup_time = t.get_or("windup_time", 0.4f);
    comp.charge_duration = t.get_or("charge_duration", 0.75f);
    comp.charge_cooldown = t.get_or("charge_cooldown", 1.5f);
    comp.charge_speed = t.get_or("charge_speed", 280.0f);
    comp.orbit_radius = t.get_or("orbit_radius", 180.0f);
    comp.flee_range = t.get_or("flee_range", 250.0f);
    comp.dynamic_separation_weight = t.get_or("dynamic_separation_weight", t.get_or("dynamicSeparationWeight", false));
    comp.is_special = t.get_or("is_special", false);

    comp.target_entity = LuaComponentHelpers::SafeGetOptionalValue<Entity>(t, "target_entity", Entity{});

    return comp;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<EnemySteeringComponent>(
        kUsertypeName, "speed", &EnemySteeringComponent::speed, "half_size", &EnemySteeringComponent::half_size,
        "halfSize", &EnemySteeringComponent::half_size, "charge_range", &EnemySteeringComponent::charge_range,
        "windup_time", &EnemySteeringComponent::windup_time, "charge_duration",
        &EnemySteeringComponent::charge_duration, "charge_cooldown", &EnemySteeringComponent::charge_cooldown,
        "charge_speed", &EnemySteeringComponent::charge_speed, "orbit_radius", &EnemySteeringComponent::orbit_radius,
        "flee_range", &EnemySteeringComponent::flee_range, "dynamic_separation_weight",
        &EnemySteeringComponent::dynamic_separation_weight, "dynamicSeparationWeight",
        &EnemySteeringComponent::dynamic_separation_weight, "is_special", &EnemySteeringComponent::is_special,
        "target_entity", &EnemySteeringComponent::target_entity);
  }
};
