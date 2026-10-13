#pragma once

#include "ECS/Entity.h"

struct HomingComponent {
  Entity target{};
  float speed = 600.0f;
  bool retarget_on_hit = false;
};
