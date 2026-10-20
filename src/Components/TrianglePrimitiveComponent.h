#pragma once

#include <glm/glm.hpp>

#include "General/BlendMode.h"
#include "General/Color.h"
#include "General/Constants.h"

// `p1`, `p2`, and `p3` are local-space vertex offsets relative to the entity's transform, scaled
// and rotated around the entity's pivot.
struct TrianglePrimitiveComponent {
  glm::vec2 p1{0.0f, 0.0f};
  glm::vec2 p2{0.0f, 0.0f};
  glm::vec2 p3{0.0f, 0.0f};
  int layer{0};
  float thickness{1.0f};  // Used when filled == false
  octarine::Color color{Constants::kUint8Max, Constants::kUint8Max, Constants::kUint8Max, Constants::kUint8Max};
  bool isFixed{true};
  bool filled{true};
  octarine::BlendMode blendMode{octarine::BlendMode::Blend};

  explicit TrianglePrimitiveComponent(const glm::vec2 t_p1 = glm::vec2(0.0f, 0.0f),
                                      const glm::vec2 t_p2 = glm::vec2(0.0f, 0.0f),
                                      const glm::vec2 t_p3 = glm::vec2(0.0f, 0.0f), const int t_layer = 0,
                                      const octarine::Color t_color = {Constants::kUint8Max, Constants::kUint8Max,
                                                                       Constants::kUint8Max, Constants::kUint8Max},
                                      const bool t_isFixed = true, const bool t_filled = true,
                                      const float t_thickness = 1.0f)
      : p1(t_p1),
        p2(t_p2),
        p3(t_p3),
        layer(t_layer),
        thickness(t_thickness),
        color(t_color),
        isFixed(t_isFixed),
        filled(t_filled) {}
};
