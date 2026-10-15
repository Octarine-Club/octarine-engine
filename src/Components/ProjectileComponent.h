#pragma once

#include "ECS/Entity.h"

struct ProjectileComponent {
  Entity target_entity{};
  // Emitter that fired this projectile (Entity{} if spawned without one). Used for damage
  // attribution in CombatDamagePlayerEvent so scripts can name the shooter, not just "projectile".
  Entity owner{};
  int damage;
  float timer;
  float duration;

  explicit ProjectileComponent(const int t_damage = 0, const float t_duration = 0.0f, const Entity t_target = Entity{})
      : target_entity(t_target), damage(t_damage), timer(0.0f), duration(t_duration) {}
};

struct ProjectileTag {};
