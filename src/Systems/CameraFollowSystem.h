#pragma once

#include <SDL3/SDL.h>

#include <algorithm>

#include "Components/CameraComponents.h"
#include "Components/CameraFollowComponent.h"
#include "Components/PositionComponent.h"
#include "ECS/Registry.h"
#include "Game/GameConfig.h"
#include "General/Constants.h"

class CameraFollowSystem {
 public:
  void operator()(const ContextFacade& context, const PositionComponent& position,
                  const CameraFollowComponent& /*follow*/) const {
    auto& camera = context.GetRegistry()->Get<CameraComponent>();
    auto& viewport = camera.viewport;
    const auto& gameConfig = context.GetRegistry()->Get<GameConfig>();
    const float zoom = camera.zoom > 0.001f ? camera.zoom : 1.0f;

    viewport.w = static_cast<float>(gameConfig.windowWidth) / zoom;
    viewport.h = static_cast<float>(gameConfig.windowHeight) / zoom;

    viewport.x = position.value.x - viewport.w / Constants::kTwo;
    viewport.y = position.value.y - viewport.h / Constants::kTwo;

    const float maxX = std::max(0.0f, gameConfig.playableAreaWidth - viewport.w);
    const float maxY = std::max(0.0f, gameConfig.playableAreaHeight - viewport.h);
    viewport.x = std::clamp(viewport.x, 0.0f, maxX);
    viewport.y = std::clamp(viewport.y, 0.0f, maxY);
  }
};