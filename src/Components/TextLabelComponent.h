#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <utility>

#include "General/Color.h"
#include "General/Constants.h"

// Alignment of the rasterized text within its layout rectangle (UIRectComponent). Only consulted on
// the UI path; world-space labels (positioned by transform) have no bounding box to align within.
enum class TextAlign : std::uint8_t { Start, Center, End };

struct TextLabelComponent {
  std::string text;
  std::string fontId;
  glm::vec2 position;
  glm::vec2 shadowOffset{0.0f, 0.0f};
  float outlineWidth = 0.0f;
  int layer;
  int wrapWidth =
      0;  // <= 0 = single-line; > 0 = wrap at pixel width (or auto-wrap to UIRect width when wrapToRect is true)
  octarine::Color color;
  octarine::Color shadowColor{0, 0, 0, 200};
  octarine::Color outlineColor{0, 0, 0, 255};
  bool isFixed;
  TextAlign hAlign = TextAlign::Start;
  TextAlign vAlign = TextAlign::Start;
  bool wrapToRect = false;

  explicit TextLabelComponent(const glm::vec2 t_position = glm::vec2(0, 0), const int t_layer = 0,
                              std::string t_text = "", std::string t_fontId = "",
                              const octarine::Color t_color = {Constants::kUint8Max, Constants::kUint8Max,
                                                               Constants::kUint8Max, Constants::kUint8Max},
                              const bool t_isFixed = true, const TextAlign t_hAlign = TextAlign::Start,
                              const TextAlign t_vAlign = TextAlign::Start)
      : text(std::move(t_text)),
        fontId(std::move(t_fontId)),
        position(t_position),
        layer(t_layer),
        color(t_color),
        isFixed(t_isFixed),
        hAlign(t_hAlign),
        vAlign(t_vAlign) {}
};
