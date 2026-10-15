#pragma once

#include "General/Rect.h"

struct CameraComponent {
  octarine::Rect viewport;
  float zoom = 1.0f;
};
