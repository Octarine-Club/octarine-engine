#pragma once

#include <cstdint>

enum RenderableType : std::uint8_t {
  SPRITE,
  SQUARE_PRIMITIVE,
  TEXT,
  CIRCLE_PRIMITIVE,
  LINE_PRIMITIVE,
  TRIANGLE_PRIMITIVE,
  CAPSULE_PRIMITIVE
};