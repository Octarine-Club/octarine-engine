#pragma once

#include <cstdint>

#include "ECS/Entity.h"

struct MagnetComponent {
  Entity reference_entity{};
  uint32_t layer = 0;
  uint32_t attracts_mask = 0;
  float attraction_radius = 120.0f;
  float speed = 360.0f;
};
