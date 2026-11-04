#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <memory>
#include <optional>

#include "AssetManager/AssetManager.h"
#include "Components/SpriteComponent.h"
#include "Components/UINineSliceComponent.h"
#include "Components/UIRectComponent.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "General/PerfUtils.h"
#include "General/SpriteFlip.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/SpriteRenderCache.h"
#include "Systems/UILayoutMetrics.h"

class RenderUISpriteSystem {
 public:
  void Prepare(Registry* registry) const {
    asset_manager_ = &registry->Get<AssetManager>();
    render_queue_ = &registry->Get<RenderQueue>();
    sprite_cache_ = &registry->Get<SpriteRenderCache>();
  }

  void operator()(const ContextFacade& ctx, Entity entity, const UIRectComponent& rect,
                  const SpriteComponent& sprite) const {
    if (!rect.visible) return;
    Registry* registry = ctx.GetRegistry();
    if (asset_manager_ == nullptr) {
      Prepare(registry);
    }

    const SpriteRenderCache::Entry* entry = LookupOrCacheSprite(entity, sprite);
    if (!entry) return;

    SDL_Texture* texture = entry->texture;
    const SDL_FRect atlasOffset = entry->atlasOffset;
    const octarine::Color activeColor =
        octarine::ui::ResolveVisualState(registry, entity, sprite.colorMod, sprite.colorMod, sprite.colorMod).bg;

    if (TryEmitNineSlice(registry, *render_queue_, entity, rect, sprite, texture, atlasOffset, activeColor)) {
      return;
    }

    const glm::vec2 srcDim = ResolveSourceDimensions(texture, sprite, atlasOffset, 0.0f, 0.0f);
    EmitQuad(*render_queue_, rect, sprite, texture, rect.left, rect.top, rect.Width(), rect.Height(),
             sprite.srcRect.x + atlasOffset.x, sprite.srcRect.y + atlasOffset.y, srcDim.x, srcDim.y, activeColor);
  }

  static void EmitNineSlice(RenderQueue& renderQueue, const UIRectComponent& rect, const SpriteComponent& sprite,
                            const UINineSliceComponent& nine, SDL_Texture* texture, const SDL_FRect& atlasOffset,
                            const octarine::Color& activeColor = {255, 255, 255, 255}) {
    const float dstW = rect.Width();
    const float dstH = rect.Height();
    const glm::vec2 srcDim = ResolveSourceDimensions(texture, sprite, atlasOffset, dstW, dstH);
    const float srcW = srcDim.x;
    const float srcH = srcDim.y;
    if (dstW <= 0.0f || dstH <= 0.0f || srcW <= 0.0f || srcH <= 0.0f) return;

    const float srcX = sprite.srcRect.x + atlasOffset.x;
    const float srcY = sprite.srcRect.y + atlasOffset.y;
    const float sl = std::clamp(nine.left, 0.0f, std::min(srcW * 0.5f, dstW * 0.5f));
    const float sr = std::clamp(nine.right, 0.0f, std::min(srcW - sl, dstW - sl));
    const float st = std::clamp(nine.top, 0.0f, std::min(srcH * 0.5f, dstH * 0.5f));
    const float sb = std::clamp(nine.bottom, 0.0f, std::min(srcH - st, dstH - st));

    const float sx[3] = {srcX, srcX + sl, srcX + srcW - sr};
    const float sw[3] = {sl, std::max(0.0f, srcW - sl - sr), sr};
    const float sy[3] = {srcY, srcY + st, srcY + srcH - sb};
    const float sh[3] = {st, std::max(0.0f, srcH - st - sb), sb};

    const float dx[3] = {rect.left, rect.left + sl, rect.right - sr};
    const float dw[3] = {sl, std::max(0.0f, dstW - sl - sr), sr};
    const float dy[3] = {rect.top, rect.top + st, rect.bottom - sb};
    const float dh[3] = {st, std::max(0.0f, dstH - st - sb), sb};

    for (int idx = 0; idx < 9; ++idx) {
      const int row = idx / 3;
      const int col = idx % 3;
      if (dw[col] > 0.0f && dh[row] > 0.0f && sw[col] > 0.0f && sh[row] > 0.0f) {
        EmitQuad(renderQueue, rect, sprite, texture, dx[col], dy[row], dw[col], dh[row], sx[col], sy[row], sw[col],
                 sh[row], activeColor);
      }
    }
  }

 private:
  const SpriteRenderCache::Entry* LookupOrCacheSprite(const Entity entity, const SpriteComponent& sprite) const {
    const auto gen = asset_manager_->TextureGeneration();
    const SpriteRenderCache::Entry* entry = sprite_cache_->Lookup(entity, gen);
    if (entry && entry->texture) {
      return entry;
    }
    SDL_Texture* texture = asset_manager_->GetTexture(sprite.assetId);
    const auto slice = asset_manager_->GetAtlasSlice(sprite.assetId);
    const SDL_FRect atlasOffset = slice.has_value() ? *slice : SDL_FRect{0, 0, 0, 0};
    sprite_cache_->Store(entity, texture, atlasOffset, gen);
    return sprite_cache_->Lookup(entity, gen);
  }

  static bool TryEmitNineSlice(Registry* registry, RenderQueue& renderQueue, const Entity entity,
                               const UIRectComponent& rect, const SpriteComponent& sprite, SDL_Texture* texture,
                               const SDL_FRect& atlasOffset, const octarine::Color& activeColor) {
    if (!registry->HasComponent<UINineSliceComponent>(entity)) return false;
    const auto& nine = registry->GetComponent<UINineSliceComponent>(entity);
    if (!nine.HasSlices()) return false;
    EmitNineSlice(renderQueue, rect, sprite, nine, texture, atlasOffset, activeColor);
    return true;
  }

  static glm::vec2 ResolveSourceDimensions(SDL_Texture* texture, const SpriteComponent& sprite,
                                           const SDL_FRect& atlasOffset, const float fallbackW, const float fallbackH) {
    float srcW = sprite.srcRect.w > 0.0f ? sprite.srcRect.w : atlasOffset.w;
    float srcH = sprite.srcRect.h > 0.0f ? sprite.srcRect.h : atlasOffset.h;
    if (srcW > 0.0f && srcH > 0.0f) {
      return {srcW, srcH};
    }
    float texW = 0.0f;
    float texH = 0.0f;
    const bool hasTexSize = SDL_GetTextureSize(texture, &texW, &texH);
    const float resolvedFallbackW = hasTexSize ? texW : fallbackW;
    const float resolvedFallbackH = hasTexSize ? texH : fallbackH;
    return {srcW > 0.0f ? srcW : resolvedFallbackW, srcH > 0.0f ? srcH : resolvedFallbackH};
  }

  static void EmitQuad(RenderQueue& renderQueue, const UIRectComponent& rect, const SpriteComponent& sprite,
                       SDL_Texture* texture, const float dx, const float dy, const float dw, const float dh,
                       const float sx, const float sy, const float sw, const float sh,
                       const octarine::Color& activeColor) {
    static constexpr float kHalf = 0.5f;
    auto& cmd = renderQueue.EmplaceSprite(static_cast<unsigned int>(rect.layer), rect.top, texture, sprite.blendMode,
                                          rect.clipId);
    cmd.destX = dx;
    cmd.destY = dy;
    cmd.destW = dw;
    cmd.destH = dh;
    cmd.srcRect = {sx, sy, sw, sh};
    cmd.rotation = 0.0f;
    cmd.pivot = {dw * kHalf, dh * kHalf};
    cmd.flip = static_cast<SDL_FlipMode>(sprite.flip);
    cmd.texture = texture;
    cmd.colorMod = SDL_Color{activeColor.r, activeColor.g, activeColor.b, activeColor.a};
    cmd.blendMode = octarine::ToSdlBlendMode(sprite.blendMode);
  }

  mutable AssetManager* asset_manager_ = nullptr;
  mutable RenderQueue* render_queue_ = nullptr;
  mutable SpriteRenderCache* sprite_cache_ = nullptr;
};
