#pragma once

#include <cstdint>
#include <type_traits>

enum class UIUnit : std::uint8_t { Auto = 0, Px = 1, Percent = 2, Fr = 3 };

struct UILayoutBoxComponent {
  float width = 0.0f;
  float height = 0.0f;
  float minWidth = 0.0f;
  float maxWidth = 0.0f;
  float minHeight = 0.0f;
  float maxHeight = 0.0f;
  float marginLeft = 0.0f;
  float marginTop = 0.0f;
  float marginRight = 0.0f;
  float marginBottom = 0.0f;
  UIUnit widthUnit = UIUnit::Auto;
  UIUnit heightUnit = UIUnit::Auto;
  UIUnit minWidthUnit = UIUnit::Auto;
  UIUnit maxWidthUnit = UIUnit::Auto;
  UIUnit minHeightUnit = UIUnit::Auto;
  UIUnit maxHeightUnit = UIUnit::Auto;
  bool visible = true;
  std::uint8_t reserved = 0;

  void SetWidthPx(const float px) {
    width = px;
    widthUnit = UIUnit::Px;
  }
  void SetHeightPx(const float px) {
    height = px;
    heightUnit = UIUnit::Px;
  }
  void SetWidthPercent(const float pct) {
    width = pct;
    widthUnit = UIUnit::Percent;
  }
  void SetHeightPercent(const float pct) {
    height = pct;
    heightUnit = UIUnit::Percent;
  }
  void SetWidthFr(const float fr) {
    width = fr;
    widthUnit = UIUnit::Fr;
  }
  void SetHeightFr(const float fr) {
    height = fr;
    heightUnit = UIUnit::Fr;
  }
  void SetMargin(const float m) {
    marginLeft = m;
    marginTop = m;
    marginRight = m;
    marginBottom = m;
  }
};

static_assert(std::is_trivially_copyable_v<UILayoutBoxComponent>);
static_assert(sizeof(UILayoutBoxComponent) == 48);
