#pragma once

#include <algorithm>
#include <type_traits>

struct UIScrollComponent {
  float scrollX = 0.0f;
  float scrollY = 0.0f;
  float contentWidth = 0.0f;
  float contentHeight = 0.0f;
  float wheelStep = 36.0f;
  bool scrollHorizontal = false;
  bool scrollVertical = true;
  bool pad0 = false;
  bool pad1 = false;

  void ClampToViewport(const float viewportW, const float viewportH) noexcept {
    const float maxX = std::max(0.0f, contentWidth - viewportW);
    const float maxY = std::max(0.0f, contentHeight - viewportH);
    scrollX = std::clamp(scrollX, 0.0f, maxX);
    scrollY = std::clamp(scrollY, 0.0f, maxY);
  }
};

static_assert(std::is_trivially_copyable_v<UIScrollComponent>);
static_assert(sizeof(UIScrollComponent) == 24);
