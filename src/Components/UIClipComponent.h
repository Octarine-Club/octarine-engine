#pragma once

#include <SDL3/SDL_rect.h>

#include <cstdint>
#include <type_traits>

struct UIClipComponent {
  SDL_Rect resolvedClip{0, 0, 0, 0};
  std::uint8_t clipId = 0;
  bool enabled = true;
  std::uint16_t reserved = 0;
};

static_assert(std::is_trivially_copyable_v<UIClipComponent>);
static_assert(sizeof(UIClipComponent) == 20);
