#pragma once

#include <cstdint>
#include <type_traits>

#include "ECS/Entity.h"

struct UIFocusComponent {
  Entity navUp{static_cast<EcsId>(-1)};
  Entity navDown{static_cast<EcsId>(-1)};
  Entity navLeft{static_cast<EcsId>(-1)};
  Entity navRight{static_cast<EcsId>(-1)};
  std::int32_t tabIndex = 0;
  bool isFocused = false;
  bool isFocusable = true;
  bool isChecked = false;
  bool isDisabled = false;
};

static_assert(std::is_trivially_copyable_v<UIFocusComponent>);
static_assert(sizeof(UIFocusComponent) == 40);
