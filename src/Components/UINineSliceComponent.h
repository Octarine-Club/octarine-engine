#pragma once

#include <type_traits>

struct UINineSliceComponent {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;

  explicit UINineSliceComponent(const float all = 0.0f) : left(all), top(all), right(all), bottom(all) {}
  UINineSliceComponent(const float l, const float t, const float r, const float b)
      : left(l), top(t), right(r), bottom(b) {}

  [[nodiscard]] bool HasSlices() const { return left > 0.0f || top > 0.0f || right > 0.0f || bottom > 0.0f; }
};

static_assert(std::is_trivially_copyable_v<UINineSliceComponent>);
static_assert(sizeof(UINineSliceComponent) == 16);
