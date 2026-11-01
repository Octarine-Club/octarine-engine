#pragma once

#include <cstdint>

enum RenderableType : std::uint8_t {
  SPRITE = 0,
  SQUARE_PRIMITIVE = 1,
  CIRCLE_PRIMITIVE = 2,
  LINE_PRIMITIVE = 3,
  TRIANGLE_PRIMITIVE = 4,
  CAPSULE_PRIMITIVE = 5,
  TEXT = 6
};