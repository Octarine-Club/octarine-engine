#pragma once

#include <cstdint>
#include <glm/glm.hpp>

#include "ECS/Entity.h"

enum class EnemyBehaviorType : uint8_t {
  Chase,
  Charge,
  Skitter,
  Orbit,
  SineFlyer,
  FlyThrough,
  Flee,
  Dummy,
  ChaseSpitter
};

struct EnemySteeringComponent {
  Entity target_entity{};
  glm::vec2 target_pos{0.0f};
  glm::vec2 custom_dir{0.0f};
  glm::vec2 perpDir{0.0f};
  float speed = 100.0f;
  float half_size = 16.0f;
  float state_timer = 0.0f;
  float age = 0.0f;
  int state = 0;
  float charge_range = 260.0f;
  float windup_time = 0.4f;
  float charge_duration = 0.75f;
  float charge_cooldown = 1.5f;
  float charge_speed = 280.0f;
  float orbit_radius = 180.0f;
  float orbit_dir = 1.0f;
  float flee_range = 250.0f;
  float trailInterval = 2.5f;
  EnemyBehaviorType type = EnemyBehaviorType::Chase;
  bool initialized = false;
  bool dynamic_separation_weight = false;
  bool is_special = false;
};
