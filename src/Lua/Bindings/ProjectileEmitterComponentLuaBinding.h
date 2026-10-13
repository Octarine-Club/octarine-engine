#pragma once

#include <sol/sol.hpp>

#include "Components/ProjectileEmitterComponent.h"
#include "General/Color.h"
#include "General/Constants.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<ProjectileEmitterComponent> {
  static constexpr const char* kLuaKey = "projectile_emitter";
  static constexpr const char* kUsertypeName = "projectile_emitter_component";

  static ProjectileEmitterComponent fromLua(const sol::object& data) {
    const auto t = data.as<sol::table>();
    using namespace LuaComponentHelpers;
    glm::vec2 projectileVelocity{};
    if (t["projectile_velocity"].valid()) {
      projectileVelocity = SafeGetVec2(t, "projectile_velocity");
    } else if (t["velocity"].valid()) {
      projectileVelocity = SafeGetVec2(t, "velocity");
    }
    // repeat_frequency drives the engine ProjectileEmitSystem auto-fire tick (interval in seconds).
    // Defaults to 0 == no auto-fire, so an emitter only auto-fires when a scene opts in with a
    // positive value; omitting it leaves firing to manual fire_projectile / scripted logic.
    const auto repeatFrequency = t["repeat_frequency"].valid()
                                     ? SafeGetOptionalValue<float>(t, "repeat_frequency", 0.0f)
                                     : SafeGetOptionalValue<float>(t, "frequency", 0.0f);
    const auto projectileDuration = t["projectile_duration"].valid()
                                        ? SafeGetOptionalValue<float>(t, "projectile_duration", 1.0f)
                                        : SafeGetOptionalValue<float>(t, "duration", 1.0f);
    const int projectileDamage = t["hit_damage"].valid() ? SafeGetOptionalValue<int>(t, "hit_damage", 10)
                                                         : SafeGetOptionalValue<int>(t, "damage", 10);
    const auto collisionMask = SafeGetOptionalValue<int>(t, "collision_mask", Constants::kDefaultEntityMask);
    const auto collisionMaskBits = EntityMask(static_cast<unsigned long long>(collisionMask));
    const auto projectile_mask = SafeGetOptionalValue<int>(t, "projectile_mask", 0);
    const auto projectileMaskBits = EntityMask(static_cast<unsigned long long>(projectile_mask));
    const auto projectile_name = SafeGetOptionalValue<std::string>(t, "projectile_name", std::string{});

    auto component =
        ProjectileEmitterComponent(projectileVelocity, projectileDuration, repeatFrequency, projectileDamage,
                                   collisionMaskBits, projectileMaskBits, projectile_name.c_str());

    // Optional override for the initial countdown timer. Defaults to repeat_frequency
    // (the C++ constructor default). Stress tests use this to stagger emitter fire times
    // so they don't all spawn projectiles on the same frame (thundering-herd).
    component.count_down_timer = SafeGetOptionalValue<float>(t, "countdown_timer", repeatFrequency);
    component.size = SafeGetOptionalValue<float>(t, "size", 10.0f);

    if (t["color"].valid()) {
      sol::table colorTable = t["color"];
      component.color.r = SafeGetOptionalValue<unsigned char>(colorTable, "r", 255);
      component.color.g = SafeGetOptionalValue<unsigned char>(colorTable, "g", 255);
      component.color.b = SafeGetOptionalValue<unsigned char>(colorTable, "b", 255);
      component.color.a = SafeGetOptionalValue<unsigned char>(colorTable, "a", 255);
    }
    component.aim_at_target = t.get_or("aim_at_target", false);
    component.is_hazard = t.get_or("is_hazard", false);
    component.target_entity = LuaComponentHelpers::SafeGetOptionalValue<Entity>(t, "target_entity", Entity{});
    return component;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<ProjectileEmitterComponent>(
        kUsertypeName, "velocity", &ProjectileEmitterComponent::velocity, "projectile_velocity",
        &ProjectileEmitterComponent::velocity, "duration", &ProjectileEmitterComponent::duration, "projectile_duration",
        &ProjectileEmitterComponent::duration, "frequency", &ProjectileEmitterComponent::frequency, "repeat_frequency",
        &ProjectileEmitterComponent::frequency, "damage", &ProjectileEmitterComponent::damage, "hit_damage",
        &ProjectileEmitterComponent::damage, "countdown_timer", &ProjectileEmitterComponent::count_down_timer,
        "projectile_name", &ProjectileEmitterComponent::projectile_name, "size", &ProjectileEmitterComponent::size,
        "color", &ProjectileEmitterComponent::color, "aim_at_target", &ProjectileEmitterComponent::aim_at_target,
        "is_hazard", &ProjectileEmitterComponent::is_hazard, "target_entity",
        &ProjectileEmitterComponent::target_entity);
  }
};
