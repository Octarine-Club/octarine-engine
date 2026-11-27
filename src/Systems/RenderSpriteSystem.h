#pragma once

#include <SDL3/SDL.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>

#include "AssetManager/AssetManager.h"
#include "Components/CameraComponents.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/SpriteComponent.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "Game/GameConfig.h"
#include "General/PerfUtils.h"
#include "General/Rect.h"
#include "General/SpriteFlip.h"
#include "Renderer/RenderCulling.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/SpriteRenderCache.h"

class RenderSpriteSystem {
 public:
  void Prepare(Registry* registry) {
    const auto& gameConfig = registry->Get<GameConfig>();
    const auto& cameraComp = registry->Get<CameraComponent>();
    camera_ = cameraComp.viewport;
    zoom_ = cameraComp.zoom > 0.001f ? cameraComp.zoom : 1.0f;
    assetManager_ = &registry->Get<AssetManager>();
    renderQueue_ = &registry->Get<RenderQueue>();
    spriteCache_ = &registry->Get<SpriteRenderCache>();
    windowWidth_ = static_cast<float>(gameConfig.windowWidth);
    windowHeight_ = static_cast<float>(gameConfig.windowHeight);

    static constexpr std::uint32_t kSweepInterval = 64;
    if (++sweep_counter_ >= kSweepInterval) {
      sweep_counter_ = 0;
      spriteCache_->SweepDead(
          [registry](const Entity e) { return registry->IsAlive(e) && registry->HasComponent<SpriteComponent>(e); });
    }

    // Single-threaded cache warm: walk sprite entities and validate both TextureGeneration() and
    // assetId hash in the flat entity-slot vector so operator() can perform lock-free array reads.
    if (!warmingQuery_) {
      warmingQuery_ = registry->CreateQuery<SpriteComponent>();
    }
    warmingQuery_->Update();
    const auto assetGen = assetManager_->TextureGeneration();
    warmingQuery_->ForEach([this, assetGen](Entity entity, const SpriteComponent& sprite) {
      const std::uint64_t idHash = SpriteRenderCache::HashAssetId(sprite.assetId);
      if (spriteCache_->LookupByHash(entity, assetGen, idHash) == nullptr) {
        SDL_Texture* texture = assetManager_->GetTexture(sprite.assetId);
        const auto slice = assetManager_->GetAtlasSlice(sprite.assetId);
        const SDL_FRect atlasOffset = slice.has_value() ? *slice : SDL_FRect{0, 0, 0, 0};
        spriteCache_->StoreByHash(entity, texture, atlasOffset, assetGen, idHash);
      }
    });

#ifdef OCTARINE_PROFILING
    if (!culledCounter_) culledCounter_ = PROFILE_COUNTER_HANDLE("RenderSprite: Culled");
    if (!emplacedCounter_) emplacedCounter_ = PROFILE_COUNTER_HANDLE("RenderSprite: Emplaced");
#endif
  }

  void operator()(Entity entity, const GlobalTransformComponent& transform, const SpriteComponent& sprite) const {
    const bool isOutsideCamera = IsRotatedQuadOutsideViewport(
        transform.position.x, transform.position.y, sprite.width * transform.scale.x, sprite.height * transform.scale.y,
        transform.pivot.x, transform.pivot.y, transform.rotation, sprite.isFixed, camera_, windowWidth_, windowHeight_);

    if (isOutsideCamera) {
      PROFILE_COUNTER_INC(culledCounter_);
      return;
    }

    PROFILE_COUNTER_INC(emplacedCounter_);

    const SpriteRenderCache::Entry* entry =
        spriteCache_->Lookup(entity, assetManager_->TextureGeneration(), sprite.assetId);
    SDL_Texture* texture = entry ? entry->texture : nullptr;
    const SDL_FRect atlasOffset = entry ? entry->atlasOffset : SDL_FRect{0, 0, 0, 0};

    const float x = sprite.isFixed ? transform.position.x : (transform.position.x - camera_.x) * zoom_;
    const float y = sprite.isFixed ? transform.position.y : (transform.position.y - camera_.y) * zoom_;
    const float destW = sprite.isFixed ? sprite.width * transform.scale.x : sprite.width * transform.scale.x * zoom_;
    const float destH = sprite.isFixed ? sprite.height * transform.scale.y : sprite.height * transform.scale.y * zoom_;
    const SDL_FRect destRect = SnapFixedUnrotated({x, y, destW, destH}, sprite.isFixed, transform.rotation);

    auto& cmd = renderQueue_->EmplaceSprite(static_cast<unsigned int>(sprite.layer), transform.position.y, texture,
                                            sprite.blendMode);
    cmd.destX = destRect.x;
    cmd.destY = destRect.y;
    cmd.destW = destRect.w;
    cmd.destH = destRect.h;
    cmd.srcRect.x = sprite.srcRect.x + atlasOffset.x;
    cmd.srcRect.y = sprite.srcRect.y + atlasOffset.y;
    cmd.srcRect.w = sprite.srcRect.w;
    cmd.srcRect.h = sprite.srcRect.h;
    cmd.rotation = transform.rotation;
    cmd.pivot = sprite.isFixed ? SDL_FPoint{transform.pivot.x, transform.pivot.y}
                               : SDL_FPoint{transform.pivot.x * zoom_, transform.pivot.y * zoom_};
    cmd.flip = static_cast<SDL_FlipMode>(sprite.flip);
    cmd.texture = texture;
    cmd.colorMod = SDL_Color{sprite.colorMod.r, sprite.colorMod.g, sprite.colorMod.b, sprite.colorMod.a};
    cmd.blendMode = octarine::ToSdlBlendMode(sprite.blendMode);
  }

 private:
  static SDL_FRect SnapFixedUnrotated(const SDL_FRect rect, const bool isFixed, const float rotation) {
    if (!isFixed || rotation != 0.0f) {
      return rect;
    }
    return {std::floor(rect.x + 0.5f), std::floor(rect.y + 0.5f), std::floor(rect.w + 0.5f), std::floor(rect.h + 0.5f)};
  }

  AssetManager* assetManager_ = nullptr;
  RenderQueue* renderQueue_ = nullptr;
  SpriteRenderCache* spriteCache_ = nullptr;
  std::unique_ptr<ComponentQuery<SpriteComponent>> warmingQuery_;
  std::uint32_t sweep_counter_ = 0;
  float windowWidth_ = 0;
  float windowHeight_ = 0;
  octarine::Rect camera_{};
  float zoom_ = 1.0f;
#ifdef OCTARINE_PROFILING
  std::atomic<long long>* culledCounter_ = nullptr;
  std::atomic<long long>* emplacedCounter_ = nullptr;
#endif
};
