#pragma once

#include <cstdint>

enum class FlexDirection : std::uint8_t { Row = 0, Column = 1 };
enum class FlexAlign : std::uint8_t { Stretch = 0, Start = 1, Center = 2, End = 3, SpaceBetween = 4, SpaceAround = 5 };

struct UIFlexComponent {
  FlexDirection direction = FlexDirection::Row;
  float gap = 0.0f;
  float crossGap = -1.0f;
  float paddingLeft = 0.0f;
  float paddingTop = 0.0f;
  float paddingRight = 0.0f;
  float paddingBottom = 0.0f;
  std::uint16_t columns = 0;
  FlexAlign align = FlexAlign::Stretch;
  FlexAlign justify = FlexAlign::Stretch;
  bool wrap = false;

  [[nodiscard]] float EffectiveCrossGap() const { return crossGap >= 0.0f ? crossGap : gap; }

  explicit UIFlexComponent(const FlexDirection t_direction = FlexDirection::Row, const float t_gap = 0.0f,
                           const float t_padding = 0.0f, const FlexAlign t_align = FlexAlign::Stretch,
                           const FlexAlign t_justify = FlexAlign::Stretch, const bool t_wrap = false,
                           const std::uint16_t t_columns = 0, const float t_crossGap = -1.0f)
      : direction(t_direction),
        gap(t_gap),
        crossGap(t_crossGap),
        paddingLeft(t_padding),
        paddingTop(t_padding),
        paddingRight(t_padding),
        paddingBottom(t_padding),
        columns(t_columns),
        align(t_align),
        justify(t_justify),
        wrap(t_wrap) {}
};
