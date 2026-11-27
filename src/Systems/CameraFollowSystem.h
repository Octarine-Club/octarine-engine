#pragma once

#include <SDL3/SDL.h>

#include <algorithm>

#include "Components/CameraComponents.h"
#include "Components/CameraFollowComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/PositionComponent.h"
#include "ECS/Registry.h"
#include "Game/GameConfig.h"
#include "General/Constants.h"

class CameraFollowSystem {
 public:
  void operator()(const ContextFacade& context, const PositionComponent& position,
                  const CameraFollowComponent& /*follow*/) const {
    auto* registry = context.GetRegistry();
    auto& camera = registry->Get<CameraComponent>();
    auto& viewport = camera.viewport;
    const auto& gameConfig = registry->Get<GameConfig>();
    const float zoom = camera.zoom > 0.001f ? camera.zoom : 1.0f;

    viewport.w = static_cast<float>(gameConfig.windowWidth) / zoom;
    viewport.h = static_cast<float>(gameConfig.windowHeight) / zoom;

    glm::vec2 targetPos = position.value;
    const Entity entity = context.GetEntity();
    if (registry->IsAlive(entity)) {
      const auto [archetype, chunkIdx, indexInChunk] = registry->GetEntityLocation(entity);
      const auto globalOpt = registry->TryComponent<GlobalTransformComponent>();
      if (archetype != nullptr && globalOpt.has_value()) {
        if (const auto* globalArr =
                archetype->TryGetComponentArray<GlobalTransformComponent>(chunkIdx, globalOpt->GetId())) {
          targetPos = globalArr[indexInChunk].position;
        }
      }
    }

    viewport.x = targetPos.x - viewport.w / Constants::kTwo;
    viewport.y = targetPos.y - viewport.h / Constants::kTwo;

    const float maxX = std::max(0.0f, gameConfig.playableAreaWidth - viewport.w);
    const float maxY = std::max(0.0f, gameConfig.playableAreaHeight - viewport.h);
    viewport.x = std::clamp(viewport.x, 0.0f, maxX);
    viewport.y = std::clamp(viewport.y, 0.0f, maxY);
  }
};