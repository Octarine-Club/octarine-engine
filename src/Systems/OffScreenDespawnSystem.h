#pragma once

#include <algorithm>
#include <glm/glm.hpp>

#include "Components/CameraFollowComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/PositionComponent.h"
#include "Components/ScaleComponent.h"
#include "Components/SpriteComponent.h"
#include "ECS/CommandBuffer.h"
#include "ECS/Entity.h"
#include "ECS/Registry.h"
#include "Game/GameConfig.h"

class OffScreenDespawnSystem {
 public:
  CommandBuffer& GetCommandBuffer() { return cmd_buffer_; }

  bool Prepare(Registry* registry) {
    if (registry->IsPaused()) return false;
    registry_ = registry;
    const auto& gameConfig = registry_->Get<GameConfig>();

    const auto windowWidth = static_cast<float>(gameConfig.windowWidth);
    const auto windowHeight = static_cast<float>(gameConfig.windowHeight);
    playableAreaWidth_ = static_cast<float>(gameConfig.playableAreaWidth);
    playableAreaHeight_ = static_cast<float>(gameConfig.playableAreaHeight);
    if (playableAreaWidth_ <= 0) playableAreaWidth_ = windowWidth;
    if (playableAreaHeight_ <= 0) playableAreaHeight_ = windowHeight;
    return true;
  }

  void operator()(const Entity entity, const PositionComponent& position, const SpriteComponent& sprite,
                  const GlobalTransformComponent* globalTransform, const ScaleComponent* scaleComp,
                  const CameraFollowComponent* cameraFollow) const {
    if (cameraFollow != nullptr) return;

    const glm::vec2 globalPos = globalTransform != nullptr ? globalTransform->position : position.value;
    const glm::vec2 globalScale = globalTransform != nullptr ? globalTransform->scale
                                  : scaleComp != nullptr     ? scaleComp->value
                                                             : glm::vec2(1.0f, 1.0f);

    const float extentX = sprite.width * globalScale.x;
    const float extentY = sprite.height * globalScale.y;
    const float minX = std::min(globalPos.x, globalPos.x + extentX);
    const float maxX = std::max(globalPos.x, globalPos.x + extentX);
    const float minY = std::min(globalPos.y, globalPos.y + extentY);
    const float maxY = std::max(globalPos.y, globalPos.y + extentY);
    if (minX > playableAreaWidth_ || minY > playableAreaHeight_ || maxX < 0.0f || maxY < 0.0f) {
      cmd_buffer_.EmplaceDespawn(entity);
    }
  }

  /// Fallback overload for legacy callers registering `<PositionComponent, SpriteComponent>`
  /// without `Opt<GlobalTransformComponent>, Opt<ScaleComponent>, Opt<CameraFollowComponent>`.
  void operator()(const Entity entity, const PositionComponent& position, const SpriteComponent& sprite) const {
    if (!registry_) {
      (*this)(entity, position, sprite, nullptr, nullptr, nullptr);
      return;
    }
    const auto [archetype, chunkIdx, indexInChunk] = registry_->GetEntityLocation(entity);
    if (!archetype) return;
    (*this)(entity, position, sprite,
            ResolveOptionalComponent<GlobalTransformComponent>(*registry_, archetype, chunkIdx, indexInChunk),
            ResolveOptionalComponent<ScaleComponent>(*registry_, archetype, chunkIdx, indexInChunk),
            ResolveOptionalComponent<CameraFollowComponent>(*registry_, archetype, chunkIdx, indexInChunk));
  }

 private:
  template <typename T>
  static const T* ResolveOptionalComponent(const Registry& registry, const Archetype* archetype, const size_t chunkIdx,
                                           const size_t indexInChunk) {
    const auto opt = registry.TryComponent<T>();
    if (!opt.has_value()) return nullptr;
    const auto* arr = archetype->TryGetComponentArray<T>(chunkIdx, opt->GetId());
    return arr ? &arr[indexInChunk] : nullptr;
  }

  Registry* registry_ = nullptr;
  CommandBuffer cmd_buffer_;
  float playableAreaWidth_ = 0.0f;
  float playableAreaHeight_ = 0.0f;
};
