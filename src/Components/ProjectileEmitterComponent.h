#pragma once

#include <cstring>
#include <glm/glm.hpp>
#include <utility>

#include "ECS/Entity.h"
#include "General/Color.h"
#include "General/Constants.h"

struct ProjectileEmitterComponent {
  char projectile_name[64] = {0};
  Entity target_entity{};
  EntityMask collisionMask;
  EntityMask projectile_mask;
  glm::vec2 velocity;
  float duration;
  float frequency;
  int damage;
  float count_down_timer;
  float size = 10.0f;
  octarine::Color color = octarine::Color{255, 60, 220, 255};
  bool aim_at_target = false;
  bool is_hazard = false;

  // t_frequency defaults to 0 == auto-fire OFF. The engine ProjectileEmitSystem tick only fires
  // emitters with frequency > 0; a positive value opts an emitter into engine-driven auto-fire
  // (interval in seconds). Scenes that drive firing manually via fire_projectile leave it at 0.
  explicit ProjectileEmitterComponent(const glm::vec2 t_velocity = glm::vec2(0, 0), const float t_duration = 1.0f,
                                      const float t_frequency = 0.0f, const int t_damage = 10,
                                      const EntityMask t_collisionMask = Constants::kDefaultEntityMask,
                                      const EntityMask t_projectileMask = EntityMask{},
                                      const char* t_projectileName = "", const bool t_aimAtTarget = false)
      : collisionMask(t_collisionMask),
        projectile_mask(t_projectileMask),
        velocity(t_velocity),
        duration(t_duration),
        frequency(t_frequency),
        damage(t_damage),
        count_down_timer(t_frequency),
        aim_at_target(t_aimAtTarget) {
    std::strncpy(projectile_name, t_projectileName, sizeof(projectile_name) - 1);
  }
};