#pragma once

#include <glm/glm.hpp>

#include "General/BlendMode.h"
#include "General/Color.h"
#include "General/Constants.h"

// Unlike SquarePrimitiveComponent (where position is a top-left offset), `position` is the circle's
// center offset relative to the entity's transform, scaled and rotated around the entity's pivot.
struct CirclePrimitiveComponent {
  glm::vec2 position{0.0f, 0.0f};
  int layer{0};
  float radius{0.0f};
  float thickness{1.0f};  // Used when filled == false
  octarine::Color color{Constants::kUint8Max, Constants::kUint8Max, Constants::kUint8Max, Constants::kUint8Max};
  bool isFixed{true};
  bool filled{true};
  octarine::BlendMode blendMode{octarine::BlendMode::Blend};

  explicit CirclePrimitiveComponent(const glm::vec2 t_position = glm::vec2(0.0f, 0.0f), const int t_layer = 0,
                                    const float t_radius = 0.0f,
                                    const octarine::Color t_color = {Constants::kUint8Max, Constants::kUint8Max,
                                                                     Constants::kUint8Max, Constants::kUint8Max},
                                    const bool t_isFixed = true, const bool t_filled = true,
                                    const float t_thickness = 1.0f)
      : position(t_position),
        layer(t_layer),
        radius(t_radius),
        thickness(t_thickness),
        color(t_color),
        isFixed(t_isFixed),
        filled(t_filled) {}
};
