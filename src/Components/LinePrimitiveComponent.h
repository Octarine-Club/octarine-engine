#pragma once

#include <glm/glm.hpp>

#include "General/BlendMode.h"
#include "General/Color.h"
#include "General/Constants.h"

// `start` and `end` are local-space offsets relative to the entity's transform, scaled and rotated
// around the entity's pivot.
struct LinePrimitiveComponent {
  glm::vec2 start{0.0f, 0.0f};
  glm::vec2 end{0.0f, 0.0f};
  int layer{0};
  float thickness{1.0f};
  octarine::Color color{Constants::kUint8Max, Constants::kUint8Max, Constants::kUint8Max, Constants::kUint8Max};
  bool isFixed{true};
  octarine::BlendMode blendMode{octarine::BlendMode::Blend};

  explicit LinePrimitiveComponent(const glm::vec2 t_start = glm::vec2(0.0f, 0.0f),
                                  const glm::vec2 t_end = glm::vec2(0.0f, 0.0f), const int t_layer = 0,
                                  const float t_thickness = 1.0f,
                                  const octarine::Color t_color = {Constants::kUint8Max, Constants::kUint8Max,
                                                                   Constants::kUint8Max, Constants::kUint8Max},
                                  const bool t_isFixed = true)
      : start(t_start), end(t_end), layer(t_layer), thickness(t_thickness), color(t_color), isFixed(t_isFixed) {}
};
