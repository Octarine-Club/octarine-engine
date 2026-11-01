#pragma once

#include <cstdint>
#include <type_traits>

#include "General/Color.h"

struct UIStylePatch {
  octarine::Color bgColor{0, 0, 0, 0};
  octarine::Color borderColor{0, 0, 0, 0};
  octarine::Color textColor{0, 0, 0, 0};
  std::uint8_t opacity = 255;
  std::uint8_t flags = 0;
  std::uint16_t reserved = 0;

  static constexpr std::uint8_t kHasBg = 1u << 0;
  static constexpr std::uint8_t kHasBorder = 1u << 1;
  static constexpr std::uint8_t kHasText = 1u << 2;
  static constexpr std::uint8_t kHasOpacity = 1u << 3;

  [[nodiscard]] bool HasBg() const noexcept { return (flags & kHasBg) != 0; }
  [[nodiscard]] bool HasBorder() const noexcept { return (flags & kHasBorder) != 0; }
  [[nodiscard]] bool HasText() const noexcept { return (flags & kHasText) != 0; }
  [[nodiscard]] bool HasOpacity() const noexcept { return (flags & kHasOpacity) != 0; }

  void SetBg(const octarine::Color c) noexcept {
    bgColor = c;
    flags |= kHasBg;
  }
  void SetBorder(const octarine::Color c) noexcept {
    borderColor = c;
    flags |= kHasBorder;
  }
  void SetText(const octarine::Color c) noexcept {
    textColor = c;
    flags |= kHasText;
  }
  void SetOpacity(const std::uint8_t a) noexcept {
    opacity = a;
    flags |= kHasOpacity;
  }
};

static_assert(std::is_trivially_copyable_v<UIStylePatch>);
static_assert(sizeof(UIStylePatch) == 16);

struct UIStyleStateComponent {
  UIStylePatch hover{};
  UIStylePatch press{};
  UIStylePatch focus{};
  UIStylePatch checked{};
  UIStylePatch disabled{};
};

static_assert(std::is_trivially_copyable_v<UIStyleStateComponent>);
static_assert(sizeof(UIStyleStateComponent) == 80);
