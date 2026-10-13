#pragma once

#include "ECS/Entity.h"
#include "General/Color.h"

struct RetaliationComponent {
  Entity target_entity{};
  float size = 80.0f;
  float damage = 15.0f;
  float lifetime = 0.5f;
  float tick_interval = 0.1f;
  octarine::Color color = {200, 120, 50, 200};
};
