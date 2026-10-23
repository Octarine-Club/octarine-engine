#pragma once

#include <cstdint>

enum class FlexDirection : std::uint8_t { Row = 0, Column = 1 };
enum class FlexAlign : std::uint8_t { Stretch = 0, Start = 1, Center = 2, End = 3 };

struct UIFlexComponent {
  FlexDirection direction = FlexDirection::Row;
  float gap = 0.0f;
  float paddingLeft = 0.0f;
  float paddingTop = 0.0f;
  float paddingRight = 0.0f;
  float paddingBottom = 0.0f;
  FlexAlign align = FlexAlign::Stretch;
  FlexAlign justify = FlexAlign::Stretch;

  explicit UIFlexComponent(const FlexDirection t_direction = FlexDirection::Row, const float t_gap = 0.0f,
                           const float t_padding = 0.0f, const FlexAlign t_align = FlexAlign::Stretch,
                           const FlexAlign t_justify = FlexAlign::Stretch)
      : direction(t_direction),
        gap(t_gap),
        paddingLeft(t_padding),
        paddingTop(t_padding),
        paddingRight(t_padding),
        paddingBottom(t_padding),
        align(t_align),
        justify(t_justify) {}
};
