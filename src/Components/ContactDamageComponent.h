#pragma once

#include <array>

#include "ECS/Entity.h"

struct ContactDamageComponent {
  Entity target_entity{};
  std::array<Entity, 8> active_contacts{};
  float damage = 10.0f;
  float cooldown = 1.0f;
  float current_cooldown = 0.0f;
  uint8_t active_contacts_count = 0;
};
