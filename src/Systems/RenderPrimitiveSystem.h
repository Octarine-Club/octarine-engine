#pragma once

#include <atomic>

#include "Components/CameraComponents.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/SquarePrimitiveComponent.h"
#include "ECS/Registry.h"
#include "Game/GameConfig.h"
#include "General/PerfUtils.h"
#include "General/Rect.h"
#include "General/Rotation2D.h"
#include "Renderer/RenderCulling.h"
#include "Renderer/RenderQueue.h"

class RenderPrimitiveBase {
 public:
  void Prepare(Registry* registry) {
    const auto& gameConfig = registry->Get<GameConfig>();
    const auto& cameraComp = registry->Get<CameraComponent>();
    camera_ = cameraComp.viewport;
    zoom_ = cameraComp.zoom > 0.001f ? cameraComp.zoom : 1.0f;
    renderQueue_ = &registry->Get<RenderQueue>();
    windowWidth_ = static_cast<float>(gameConfig.windowWidth);
    windowHeight_ = static_cast<float>(gameConfig.windowHeight);
#ifdef OCTARINE_PROFILING
    if (!culledCounter_) culledCounter_ = PROFILE_COUNTER_HANDLE("RenderPrimitive: Culled");
    if (!emplacedCounter_) emplacedCounter_ = PROFILE_COUNTER_HANDLE("RenderPrimitive: Emplaced");
#endif
  }

 protected:
  [[nodiscard]] bool TryCull(const float originX, const float originY, const float width, const float height,
                             const bool isFixed) const {
    if (IsRenderableOutsideViewport(originX, originY, width, height, isFixed, camera_, windowWidth_, windowHeight_)) {
      PROFILE_COUNTER_INC(culledCounter_);
      return true;
    }
    PROFILE_COUNTER_INC(emplacedCounter_);
    return false;
  }

  [[nodiscard]] static glm::vec2 TransformLocalPoint(const glm::vec2 localOffset,
                                                     const GlobalTransformComponent& transform,
                                                     const octarine::Rotation2D rot) {
    return transform.position + transform.pivot +
           octarine::Rotate(localOffset * transform.scale - transform.pivot, rot);
  }

  [[nodiscard]] glm::vec2 ToScreenPoint(const glm::vec2 worldPt, const bool isFixed) const {
    return isFixed ? worldPt : (worldPt - glm::vec2{camera_.x, camera_.y}) * zoom_;
  }

  [[nodiscard]] float ToScreenLength(const float worldLen, const bool isFixed) const {
    return isFixed ? worldLen : worldLen * zoom_;
  }

  RenderQueue* renderQueue_ = nullptr;
  float windowWidth_ = 0;
  float windowHeight_ = 0;
  octarine::Rect camera_{};
  float zoom_ = 1.0f;
#ifdef OCTARINE_PROFILING
  std::atomic<long long>* culledCounter_ = nullptr;
  std::atomic<long long>* emplacedCounter_ = nullptr;
#endif
};

class RenderPrimitiveSystem : public RenderPrimitiveBase {
 public:
  void operator()(const SquarePrimitiveComponent& square, const GlobalTransformComponent& transform) const {
    // square.position is a local offset from the entity's transform, so it scales with the
    // entity the same way BoxColliderComponent::offset does in KDTreeCollisionSystem — otherwise a
    // scaled entity's primitive grows but stays pinned at its unscaled offset.
    const glm::vec2 scaledOffset = square.position * transform.scale;
    const glm::vec2 origin = transform.position + scaledOffset;
    const float w = square.width * transform.scale.x;
    const float h = square.height * transform.scale.y;

    if (TryCull(origin.x, origin.y, w, h, square.isFixed)) {
      return;
    }

    const glm::vec2 screenOrigin = ToScreenPoint(origin, square.isFixed);
    const float destW = ToScreenLength(w, square.isFixed);
    const float destH = ToScreenLength(h, square.isFixed);

    auto& cmd = renderQueue_->EmplaceSquare(static_cast<unsigned int>(square.layer), square.position.y, nullptr,
                                            square.blendMode);
    cmd.destRect = {screenOrigin.x, screenOrigin.y, destW, destH};
    cmd.color = SDL_Color{square.color.r, square.color.g, square.color.b, square.color.a};
    cmd.rotation = transform.rotation;
    // The quad's top-left is `origin`, but pivot is an offset from transform.position, so rebase
    // it onto the destRect the renderer receives. Both are in scaled pixels.
    const glm::vec2 localPivot = transform.pivot - scaledOffset;
    cmd.pivot = square.isFixed ? SDL_FPoint{localPivot.x, localPivot.y}
                               : SDL_FPoint{localPivot.x * zoom_, localPivot.y * zoom_};
    cmd.blendMode = octarine::ToSdlBlendMode(square.blendMode);
  }
};
