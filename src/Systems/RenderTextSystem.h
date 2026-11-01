#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

#include "AssetManager/AssetManager.h"
#include "AssetManager/GlyphAtlas.h"
#include "Components/CameraComponents.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/TextLabelComponent.h"
#include "Components/UIRectComponent.h"
#include "ECS/Entity.h"
#include "ECS/Registry.h"
#include "Engine/EngineContext.h"
#include "Game/GameConfig.h"
#include "General/Logger.h"
#include "General/PerfUtils.h"
#include "General/Rect.h"
#include "Renderer/RenderCommands.h"
#include "Renderer/RenderCulling.h"
#include "Renderer/RenderQueue.h"
#include "Systems/UILayoutMetrics.h"

class RenderTextSystem {
 public:
  RenderTextSystem() = default;

  RenderTextSystem(const RenderTextSystem&) = delete;
  RenderTextSystem& operator=(const RenderTextSystem&) = delete;
  RenderTextSystem(RenderTextSystem&&) = default;
  RenderTextSystem& operator=(RenderTextSystem&&) = default;

  ~RenderTextSystem() {
    for (auto& [key, entry] : text_cache_) {
      if (entry.texture) {
        SDL_DestroyTexture(entry.texture);
      }
    }
  }

  void operator()(const ContextFacade& ctx, const TextLabelComponent& text) const {
    if (text.text.empty()) return;

    Registry* registry = ctx.GetRegistry();
    const auto& assetManager = registry->Get<AssetManager>();
    auto* sdlRenderer = registry->Get<EngineContext>().sdlRenderer;
    auto& renderQueue = registry->Get<RenderQueue>();
    const auto& cameraComp = registry->Get<CameraComponent>();
    const octarine::Rect camera = cameraComp.viewport;
    const float zoom = cameraComp.zoom > 0.001f ? cameraComp.zoom : 1.0f;

    TTF_Font* font = assetManager.GetFont(text.fontId);
    if (!font) return;

    const Entity entity = ctx.GetEntity();
    const int effectiveWrapWidth = ResolveWrapWidth(registry, entity, text);

    auto it = text_cache_.find(entity.GetId());
    const bool needsRaster = it == text_cache_.end() || it->second.fontId != text.fontId ||
                             it->second.text != text.text || it->second.wrapWidth != effectiveWrapWidth;
    if (needsRaster) {
      float w = 0;
      float h = 0;
      SDL_Texture* texture = RasterizeLabel(assetManager, sdlRenderer, font, text, effectiveWrapWidth, w, h);
      if (texture == nullptr) return;
      it = StoreEntry(it, entity, text, effectiveWrapWidth, texture, w, h);
    }

    // UIRectComponent (written by UILayoutSystem) takes priority over GlobalTransform for position.
    // text.position acts as a local pixel offset within the rect (e.g., padding).
    const auto placement = ResolvePlacement(registry, entity, text, it->second.width, it->second.height);
    if (!placement.visible) return;
    const auto& origin = placement.origin;
    const bool effectivelyFixed = placement.effectivelyFixed;
    const int renderLayer = placement.renderLayer;
    const float sortY = placement.sortY;
    const std::uint8_t clipId = placement.clipId;

    const auto& gameConfig = registry->Get<GameConfig>();
    const bool isOutsideCamera = IsRenderableOutsideViewport(
        origin.x, origin.y, it->second.width, it->second.height, effectivelyFixed, camera,
        static_cast<float>(gameConfig.windowWidth), static_cast<float>(gameConfig.windowHeight));

#ifdef OCTARINE_PROFILING
    static auto* culledCounter = PROFILE_COUNTER_HANDLE("RenderText: Culled");
    static auto* emplacedCounter = PROFILE_COUNTER_HANDLE("RenderText: Emplaced");
#endif

    if (isOutsideCamera) {
      PROFILE_COUNTER_INC(culledCounter);
      return;
    }
    PROFILE_COUNTER_INC(emplacedCounter);

    const float scale = effectivelyFixed ? 1.0f : zoom;
    const float x = effectivelyFixed ? origin.x : (origin.x - camera.x) * zoom;
    const float y = effectivelyFixed ? origin.y : (origin.y - camera.y) * zoom;
    const float w = it->second.width * scale;
    const float h = it->second.height * scale;
    const octarine::Color activeTextColor =
        octarine::ui::ResolveVisualState(registry, entity, text.color, text.color, text.color).text;

    EmitShadowIfAny(renderQueue, text, activeTextColor, it->second.texture, renderLayer, sortY, x, y, w, h, scale,
                    clipId);

    auto& cmd = renderQueue.EmplaceText(static_cast<unsigned int>(renderLayer), sortY, it->second.texture, clipId);
    cmd.destRect = {x, y, w, h};
    cmd.texture = it->second.texture;
    cmd.colorMod = SDL_Color{activeTextColor.r, activeTextColor.g, activeTextColor.b, activeTextColor.a};
  }

 private:
  static int ResolveWrapWidth(Registry* registry, const Entity entity, const TextLabelComponent& text) {
    if (text.wrapWidth > 0 || !text.wrapToRect || !registry->HasComponent<UIRectComponent>(entity)) {
      return text.wrapWidth;
    }
    return std::max(0, static_cast<int>(registry->GetComponent<UIRectComponent>(entity).Width()));
  }

  static void EmitShadowIfAny(RenderQueue& renderQueue, const TextLabelComponent& text,
                              const octarine::Color& activeTextColor, SDL_Texture* texture, const int renderLayer,
                              const float sortY, const float x, const float y, const float w, const float h,
                              const float scale, const std::uint8_t clipId) {
    if ((text.shadowOffset.x == 0.0f && text.shadowOffset.y == 0.0f) || text.shadowColor.a == 0 ||
        activeTextColor.a == 0) {
      return;
    }
    const float sx = text.shadowOffset.x * scale;
    const float sy = text.shadowOffset.y * scale;
    const auto shadowAlpha = static_cast<Uint8>((static_cast<int>(text.shadowColor.a) * activeTextColor.a) / 255);
    auto& shadowCmd = renderQueue.EmplaceText(static_cast<unsigned int>(renderLayer), sortY, texture, clipId);
    shadowCmd.destRect = {x + sx, y + sy, w, h};
    shadowCmd.texture = texture;
    shadowCmd.colorMod = SDL_Color{text.shadowColor.r, text.shadowColor.g, text.shadowColor.b, shadowAlpha};
  }
  struct TextPlacement {
    glm::vec2 origin;
    float sortY;
    bool effectivelyFixed;
    bool visible;
    int renderLayer;
    std::uint8_t clipId;
  };

  static TextPlacement ResolvePlacement(Registry* registry, const Entity entity, const TextLabelComponent& text,
                                        const float textWidth, const float textHeight) {
    glm::vec2 origin = text.position;
    float sortY = origin.y;
    bool effectivelyFixed = text.isFixed;
    bool visible = true;
    int renderLayer = text.layer;
    std::uint8_t clipId = 0;
    if (registry->HasComponent<UIRectComponent>(entity)) {
      const auto& rect = registry->GetComponent<UIRectComponent>(entity);
      visible = rect.visible;
      clipId = rect.clipId;
      const float slackX = rect.Width() - textWidth;
      const float slackY = rect.Height() - textHeight;
      const auto offsetFor = [](const TextAlign align, const float slack) {
        static constexpr float kHalf = 0.5f;
        if (align == TextAlign::Center) return slack * kHalf;
        if (align == TextAlign::End) return slack;
        return 0.0f;
      };
      origin = glm::vec2(rect.left + offsetFor(text.hAlign, slackX), rect.top + offsetFor(text.vAlign, slackY)) +
               text.position;
      sortY = rect.top;
      effectivelyFixed = true;
      renderLayer = rect.layer;
    } else if (registry->HasComponent<GlobalTransformComponent>(entity)) {
      const auto& transform = registry->GetComponent<GlobalTransformComponent>(entity);
      origin += transform.position;
      sortY = origin.y;
    }
    return {origin, sortY, effectivelyFixed, visible, renderLayer, clipId};
  }

  struct TextCacheEntry {
    std::string fontId{};
    std::string text{};
    int wrapWidth = 0;
    SDL_Texture* texture = nullptr;
    float width = 0.0F;
    float height = 0.0F;
  };

  mutable std::unordered_map<EcsId, TextCacheEntry> text_cache_;

  std::unordered_map<EcsId, TextCacheEntry>::iterator StoreEntry(std::unordered_map<EcsId, TextCacheEntry>::iterator it,
                                                                 Entity entity, const TextLabelComponent& text,
                                                                 int wrapWidth, SDL_Texture* texture, float width,
                                                                 float height) const {
    if (it != text_cache_.end()) {
      if (it->second.texture != nullptr) SDL_DestroyTexture(it->second.texture);
      it->second = {text.fontId, text.text, wrapWidth, texture, width, height};
      return it;
    }
    return text_cache_
        .emplace(entity.GetId(), TextCacheEntry{text.fontId, text.text, wrapWidth, texture, width, height})
        .first;
  }

  // Rasterize `text` in white (255,255,255,255) to a freshly created SDL_Texture, writing its size
  // into outW/outH. Per-draw colorMod in TextCommand tints the texture (and its optional shadow pass)
  // without re-rasterizing when color or alpha changes.
  static SDL_Texture* RasterizeLabel(const AssetManager& assetManager, SDL_Renderer* sdlRenderer, TTF_Font* font,
                                     const TextLabelComponent& text, const int wrapWidth, float& outW, float& outH) {
    const SDL_Color white{255, 255, 255, 255};
    SDL_Surface* surface = nullptr;
    const GlyphAtlas* atlas = assetManager.GetGlyphAtlas(text.fontId);
    const bool hasNewline = text.text.find('\n') != std::string::npos;
    if (wrapWidth <= 0 && !hasNewline && atlas != nullptr && atlas->IsLoaded()) {
      surface = ComposeFromAtlas(*atlas, text.text, white);
    }
    if (surface == nullptr) {
      if (wrapWidth > 0 || hasNewline) {
        surface = TTF_RenderText_Blended_Wrapped(font, text.text.c_str(), 0, white, wrapWidth > 0 ? wrapWidth : 0);
      } else {
        surface = TTF_RenderText_Blended(font, text.text.c_str(), 0, white);
      }
    }
    if (surface == nullptr) {
      Logger::Error("RenderText: surface compose failed: " + std::string(SDL_GetError()));
      return nullptr;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(sdlRenderer, surface);
    SDL_DestroySurface(surface);
    if (texture == nullptr) {
      Logger::Error("SDL_CreateTextureFromSurface failed: " + std::string(SDL_GetError()));
      return nullptr;
    }
    outW = 0;
    outH = 0;
    SDL_GetTextureSize(texture, &outW, &outH);
    return texture;
  }

  // Atlas compose: build an RGBA destination surface, blit each codepoint's rect from the atlas
  // into it, color-mod the source surface to tint the white-rasterized atlas glyphs to the
  // requested color. Returns nullptr on any missing codepoint (caller falls back to TTF). Layout
  // is top-aligned — not pixel-perfect for descenders, but readable for ASCII at typical UI sizes;
  // ascent-aware baseline alignment is a follow-up.
  static SDL_Surface* ComposeFromAtlas(const GlyphAtlas& atlas, const std::string& text, SDL_Color color) {
    float totalW = 0.0F;
    for (const char c : text) {
      const auto cp = static_cast<std::uint32_t>(static_cast<unsigned char>(c));
      const auto* g = atlas.Find(cp);
      if (g == nullptr) return nullptr;
      totalW += g->advance;
    }
    const int destW = std::max(1, static_cast<int>(std::ceil(totalW)));
    const int destH = std::max(1, atlas.LineSkip());
    SDL_Surface* dst = SDL_CreateSurface(destW, destH, SDL_PIXELFORMAT_RGBA32);
    if (dst == nullptr) return nullptr;

    SDL_Surface* src = atlas.SourceSurface();
    SDL_SetSurfaceColorMod(src, color.r, color.g, color.b);
    SDL_SetSurfaceAlphaMod(src, color.a);
    SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_BLEND);

    float pen = 0.0F;
    for (const char c : text) {
      const auto cp = static_cast<std::uint32_t>(static_cast<unsigned char>(c));
      const auto* g = atlas.Find(cp);
      SDL_Rect srcR{static_cast<int>(g->x), static_cast<int>(g->y), static_cast<int>(g->w), static_cast<int>(g->h)};
      SDL_Rect dstR{static_cast<int>(pen), 0, static_cast<int>(g->w), static_cast<int>(g->h)};
      SDL_BlitSurface(src, &srcR, dst, &dstR);
      pen += g->advance;
    }

    // Reset mods so the next compose (or a parallel one for a differently-colored label) starts
    // from a clean state on the shared atlas surface.
    SDL_SetSurfaceColorMod(src, 255, 255, 255);
    SDL_SetSurfaceAlphaMod(src, 255);
    return dst;
  }
};
