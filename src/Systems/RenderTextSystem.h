#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AssetManager/AssetManager.h"
#include "AssetManager/GlyphAtlas.h"
#include "Components/CameraComponents.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/TextLabelComponent.h"
#include "Components/UIRectComponent.h"
#include "ECS/Entity.h"
#include "ECS/Registry.h"
#include "Game/EngineContext.h"
#include "Game/GameConfig.h"
#include "General/Logger.h"
#include "General/PerfUtils.h"
#include "General/Rect.h"
#include "Renderer/RenderCommands.h"
#include "Renderer/RenderCulling.h"
#include "Renderer/RenderQueue.h"
#include "Systems/UILayoutMetrics.h"

// Registry-owned clearable cache singleton for rasterized text label SDL_Texture* handles (L1 entity
// cache + L2 bounded 512-entry LRU content cache + deferred retire queue synchronized against
// RenderQueue::ClearGeneration()). SceneLoader::clearSceneEntities() can clear it via
// `registry.TryGet<TextRenderCache>()->Clear(registry.TryGet<RenderQueue>())` across scene swaps.
class TextRenderCache {
 public:
  struct SharedTextTexture {
    SDL_Texture* ptr = nullptr;
    float texW = 0.0F;
    float texH = 0.0F;
    bool fromAtlas = false;

    SharedTextTexture() = default;
    SharedTextTexture(const SharedTextTexture&) = delete;
    SharedTextTexture& operator=(const SharedTextTexture&) = delete;
    SharedTextTexture(SharedTextTexture&&) = delete;
    SharedTextTexture& operator=(SharedTextTexture&&) = delete;
    ~SharedTextTexture() {
      if (ptr != nullptr) {
        SDL_DestroyTexture(ptr);
      }
    }
  };

  struct TextCacheEntry {
    std::string fontId{};
    std::string text{};
    std::uint64_t fontGen = 0;
    int wrapWidth = 0;
    float rasterScale = 1.0F;
    float labelScale = 1.0F;
    bool pixelSharp = true;
    std::shared_ptr<SharedTextTexture> shared{};
    float width = 0.0F;
    float height = 0.0F;
  };

  struct ContentKeyView {
    std::string_view fontId{};
    std::uint64_t fontGen = 0;
    int wrapWidth = 0;
    int rasterScaleKey = 100;
    int deviceScaleKey = 100;
    bool pixelSharp = true;
    std::string_view text{};
  };

  struct ContentKey {
    std::string fontId{};
    std::uint64_t fontGen = 0;
    int wrapWidth = 0;
    int rasterScaleKey = 100;
    int deviceScaleKey = 100;
    bool pixelSharp = true;
    std::string text{};
    explicit operator ContentKeyView() const noexcept {
      return {fontId, fontGen, wrapWidth, rasterScaleKey, deviceScaleKey, pixelSharp, text};
    }
  };

  struct ContentKeyPtrHash {
    using is_transparent = void;
    static std::size_t HashView(const ContentKeyView& k) noexcept {
      std::size_t h = std::hash<std::string_view>{}(k.fontId);
      h ^= std::hash<std::uint64_t>{}(k.fontGen) + 0x9e3779b9u + (h << 6) + (h >> 2);
      h ^= std::hash<int>{}(k.wrapWidth) + 0x9e3779b9u + (h << 6) + (h >> 2);
      h ^= std::hash<int>{}(k.rasterScaleKey) + 0x9e3779b9u + (h << 6) + (h >> 2);
      h ^= std::hash<int>{}(k.deviceScaleKey) + 0x9e3779b9u + (h << 6) + (h >> 2);
      h ^= std::hash<bool>{}(k.pixelSharp) + 0x9e3779b9u + (h << 6) + (h >> 2);
      h ^= std::hash<std::string_view>{}(k.text) + 0x9e3779b9u + (h << 6) + (h >> 2);
      return h;
    }
    std::size_t operator()(const ContentKey* k) const noexcept { return HashView(static_cast<ContentKeyView>(*k)); }
    std::size_t operator()(const ContentKeyView& k) const noexcept { return HashView(k); }
  };

  struct ContentKeyPtrEq {
    using is_transparent = void;
    static bool EqView(const ContentKeyView& a, const ContentKeyView& b) noexcept {
      return a.fontId == b.fontId && a.fontGen == b.fontGen && a.wrapWidth == b.wrapWidth &&
             a.rasterScaleKey == b.rasterScaleKey && a.deviceScaleKey == b.deviceScaleKey &&
             a.pixelSharp == b.pixelSharp && a.text == b.text;
    }
    bool operator()(const ContentKey* a, const ContentKey* b) const noexcept {
      return EqView(static_cast<ContentKeyView>(*a), static_cast<ContentKeyView>(*b));
    }
    bool operator()(const ContentKey* a, const ContentKeyView& b) const noexcept {
      return EqView(static_cast<ContentKeyView>(*a), b);
    }
    bool operator()(const ContentKeyView& a, const ContentKey* b) const noexcept {
      return EqView(a, static_cast<ContentKeyView>(*b));
    }
  };

  struct ContentNode {
    ContentKey key;
    std::shared_ptr<SharedTextTexture> tex;
  };

  static constexpr std::size_t kMaxContentCacheEntries = 512;

  TextRenderCache() = default;
  TextRenderCache(const TextRenderCache&) = delete;
  TextRenderCache& operator=(const TextRenderCache&) = delete;

  TextRenderCache(TextRenderCache&& other) noexcept
      : text_cache_(std::move(other.text_cache_)),
        content_lru_(std::move(other.content_lru_)),
        content_cache_(std::move(other.content_cache_)),
        retired_textures_(std::move(other.retired_textures_)),
        last_clear_gen_(other.last_clear_gen_),
        sweep_counter_(other.sweep_counter_) {}

  TextRenderCache& operator=(TextRenderCache&& other) noexcept {
    if (this != &other) {
      DestroyImmediate();
      text_cache_ = std::move(other.text_cache_);
      content_lru_ = std::move(other.content_lru_);
      content_cache_ = std::move(other.content_cache_);
      retired_textures_ = std::move(other.retired_textures_);
      last_clear_gen_ = other.last_clear_gen_;
      sweep_counter_ = other.sweep_counter_;
    }
    return *this;
  }

  ~TextRenderCache() { DestroyImmediate(); }

  // Drops both L1 entity entries and L2 LRU entries. When `renderQueue` is non-empty (mid-frame),
  // active SDL_Texture* handles move to `retired_textures_` until RenderQueue::ClearGeneration()
  // advances; otherwise they are destroyed immediately.
  void Clear(const RenderQueue* renderQueue = nullptr) {
    for (auto& [_, entry] : text_cache_) {
      RetireIfLast(entry.shared);
    }
    text_cache_.clear();
    content_cache_.clear();
    for (auto& node : content_lru_) {
      RetireIfLast(node.tex);
    }
    content_lru_.clear();
    sweep_counter_ = 0;
    if (renderQueue == nullptr || renderQueue->IsEmpty()) {
      DestroyRetiredTextures();
    }
  }

  void DestroyImmediate() {
    text_cache_.clear();
    content_cache_.clear();
    content_lru_.clear();
    DestroyRetiredTextures();
  }

  void FlushRetiredIfRenderQueueCleared(const RenderQueue* renderQueue) {
    if (renderQueue == nullptr) return;
    const std::uint64_t gen = renderQueue->ClearGeneration();
    if (gen == last_clear_gen_) return;
    last_clear_gen_ = gen;
    DestroyRetiredTextures();
  }

  void SweepDeadCacheEntries(Registry* registry) {
    static constexpr std::uint32_t kSweepInterval = 64;
    if (++sweep_counter_ < kSweepInterval) return;
    sweep_counter_ = 0;
    for (auto it = text_cache_.begin(); it != text_cache_.end();) {
      const Entity cachedEnt(it->first);
      if (!registry->IsAlive(cachedEnt) || !registry->HasComponent<TextLabelComponent>(cachedEnt)) {
        RetireIfLast(it->second.shared);
        it = text_cache_.erase(it);
      } else {
        ++it;
      }
    }
  }

  void RetireIfLast(std::shared_ptr<SharedTextTexture>& sp) {
    if (sp && sp.use_count() == 1 && sp->ptr != nullptr) {
      retired_textures_.push_back(sp->ptr);
      sp->ptr = nullptr;
    }
    sp.reset();
  }

  void EvictOldestContent() {
    if (content_lru_.empty()) return;
    auto& oldest = content_lru_.back();
    content_cache_.erase(&oldest.key);
    RetireIfLast(oldest.tex);
    content_lru_.pop_back();
  }

  [[nodiscard]] std::size_t RetiredTextureCount() const noexcept { return retired_textures_.size(); }
  [[nodiscard]] std::size_t ContentCacheSize() const noexcept { return content_lru_.size(); }

  std::unordered_map<std::uint64_t, TextCacheEntry> text_cache_{};
  std::list<ContentNode> content_lru_{};
  std::unordered_map<const ContentKey*, std::list<ContentNode>::iterator, ContentKeyPtrHash, ContentKeyPtrEq>
      content_cache_{};
  std::vector<SDL_Texture*> retired_textures_{};
  std::uint64_t last_clear_gen_ = 0;
  std::uint32_t sweep_counter_ = 0;

 private:
  void DestroyRetiredTextures() {
    for (SDL_Texture* tex : retired_textures_) {
      if (tex != nullptr) {
        SDL_DestroyTexture(tex);
      }
    }
    retired_textures_.clear();
  }
};

class RenderTextSystem {
 public:
  RenderTextSystem() = default;

  RenderTextSystem(const RenderTextSystem&) = delete;
  RenderTextSystem& operator=(const RenderTextSystem&) = delete;
  RenderTextSystem(RenderTextSystem&&) noexcept = default;
  RenderTextSystem& operator=(RenderTextSystem&&) noexcept = default;
  ~RenderTextSystem() = default;

  [[nodiscard]] std::size_t RetiredTextureCount() const noexcept {
    return text_render_cache_ != nullptr ? text_render_cache_->RetiredTextureCount() : 0;
  }

  [[nodiscard]] std::size_t ContentCacheSize() const noexcept {
    return text_render_cache_ != nullptr ? text_render_cache_->ContentCacheSize() : 0;
  }

  void ClearCache() const {
    if (text_render_cache_ != nullptr) {
      text_render_cache_->Clear(render_queue_);
    }
  }

  static void ClearRegistryCache(Registry& registry) {
    if (auto* cache = registry.TryGet<TextRenderCache>()) {
      cache->Clear(registry.TryGet<RenderQueue>());
    }
  }

  void Prepare(Registry* registry) const {
    asset_manager_ = &registry->Get<AssetManager>();
    engine_ctx_ = &registry->Get<EngineContext>();
    render_queue_ = &registry->Get<RenderQueue>();
    camera_comp_ = &registry->Get<CameraComponent>();
    game_config_ = &registry->Get<GameConfig>();
    if (auto* existing = registry->TryGet<TextRenderCache>()) {
      text_render_cache_ = existing;
    } else {
      text_render_cache_ = &registry->Set<TextRenderCache>(TextRenderCache{});
    }
    text_render_cache_->FlushRetiredIfRenderQueueCleared(render_queue_);
    text_render_cache_->SweepDeadCacheEntries(registry);
  }

  void operator()(const ContextFacade& ctx, const TextLabelComponent& text) const {
    if (text.text.empty()) return;

    Registry* registry = ctx.GetRegistry();
    if (asset_manager_ == nullptr || text_render_cache_ == nullptr) {
      Prepare(registry);
    }
    const auto& assetManager = *asset_manager_;
    const auto& engineCtx = *engine_ctx_;
    auto* sdlRenderer = engineCtx.sdlRenderer;
    auto& renderQueue = *render_queue_;
    const auto& cameraComp = *camera_comp_;
    const octarine::Rect camera = cameraComp.viewport;
    const float zoom = cameraComp.zoom > 0.001f ? cameraComp.zoom : 1.0f;

    const Entity entity = ctx.GetEntity();
    const bool hasUiRect = registry->HasComponent<UIRectComponent>(entity);
    const float labelScale = hasUiRect ? octarine::ui::ResolveUiScale(*game_config_) : 1.0F;
    const int effectiveWrapWidth = ResolveWrapWidth(registry, entity, text, hasUiRect, labelScale);
    const bool pixelSharp = assetManager.GetEffectiveScaleMode() != SDL_SCALEMODE_LINEAR;
    const float deviceScale = ResolveRasterScale(engineCtx.sceneRenderScale, pixelSharp);
    const float rasterScale = std::clamp(labelScale * deviceScale, 0.25F, 8.0F);
    const std::uint64_t fontGen = assetManager.FontGeneration();

    auto it = ResolveOrRasterize(entity, text, assetManager, sdlRenderer, fontGen, effectiveWrapWidth, rasterScale,
                                 labelScale, deviceScale, pixelSharp);
    if (it == text_render_cache_->text_cache_.end() || !it->second.shared || it->second.shared->ptr == nullptr) return;
    SDL_Texture* cachedTex = it->second.shared->ptr;

    const auto placement = ResolvePlacement(registry, entity, text, it->second.width, it->second.height, labelScale);
    if (!placement.visible) return;
    const auto& origin = placement.origin;
    const bool effectivelyFixed = placement.effectivelyFixed;
    const int renderLayer = std::max(0, placement.renderLayer);
    const float sortY = placement.sortY;
    const std::uint8_t clipId = placement.clipId;

    const auto& gameConfig = *game_config_;
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
    const glm::vec2 screenOrigin = ResolveScreenOrigin(origin, camera, zoom, effectivelyFixed, pixelSharp);
    const float x = screenOrigin.x;
    const float y = screenOrigin.y;
    const float w = it->second.width * scale;
    const float h = it->second.height * scale;
    const octarine::Color activeTextColor =
        octarine::ui::ResolveVisualState(registry, entity, text.color, text.color, text.color).text;

    EmitLabelDecorations(renderQueue, text, activeTextColor, cachedTex, renderLayer, sortY, x, y, w, h, scale,
                         labelScale, hasUiRect, pixelSharp && effectivelyFixed, clipId);

    auto& cmd = renderQueue.EmplaceText(static_cast<unsigned int>(renderLayer), sortY, cachedTex, clipId);
    cmd.destRect = {x, y, w, h};
    cmd.texture = cachedTex;
    cmd.colorMod = SDL_Color{activeTextColor.r, activeTextColor.g, activeTextColor.b, activeTextColor.a};
  }

  static void EmitShadowIfAny(RenderQueue& renderQueue, const TextLabelComponent& text,
                              const octarine::Color& activeTextColor, SDL_Texture* texture, const int renderLayer,
                              const float sortY, const float x, const float y, const float w, const float h,
                              const float scale, const std::uint8_t clipId, const bool snapOffset = false) {
    if ((text.shadowOffset.x == 0.0f && text.shadowOffset.y == 0.0f) || text.shadowColor.a == 0 ||
        activeTextColor.a == 0) {
      return;
    }
    const float rawSx = text.shadowOffset.x * scale;
    const float rawSy = text.shadowOffset.y * scale;
    const float sx = snapOffset ? std::round(rawSx) : rawSx;
    const float sy = snapOffset ? std::round(rawSy) : rawSy;
    const auto shadowAlpha = static_cast<Uint8>((static_cast<int>(text.shadowColor.a) * activeTextColor.a) / 255);
    auto& shadowCmd =
        renderQueue.EmplaceText(static_cast<unsigned int>(std::max(0, renderLayer)), sortY, texture, clipId);
    shadowCmd.destRect = {x + sx, y + sy, w, h};
    shadowCmd.texture = texture;
    shadowCmd.colorMod = SDL_Color{text.shadowColor.r, text.shadowColor.g, text.shadowColor.b, shadowAlpha};
  }

  static void EmitOutlineIfAny(RenderQueue& renderQueue, const TextLabelComponent& text,
                               const octarine::Color& activeTextColor, SDL_Texture* texture, const int renderLayer,
                               const float sortY, const float x, const float y, const float w, const float h,
                               const float scale, const std::uint8_t clipId) {
    static constexpr float kMaxOutlineWidth = 4.0f;
    EmitOutlineRings(renderQueue, text, activeTextColor, texture, renderLayer, sortY, x, y, w, h, text.outlineWidth,
                     kMaxOutlineWidth, scale, clipId);
  }

 private:
  using SharedTextTexture = TextRenderCache::SharedTextTexture;
  using TextCacheEntry = TextRenderCache::TextCacheEntry;
  using ContentKeyView = TextRenderCache::ContentKeyView;
  using ContentKey = TextRenderCache::ContentKey;
  using ContentNode = TextRenderCache::ContentNode;

  static void EmitLabelDecorations(RenderQueue& renderQueue, const TextLabelComponent& text,
                                   const octarine::Color& activeTextColor, SDL_Texture* texture, const int renderLayer,
                                   const float sortY, const float x, const float y, const float w, const float h,
                                   const float scale, const float labelScale, const bool hasUiRect,
                                   const bool snapShadow, const std::uint8_t clipId) {
    const float shadowScale = hasUiRect ? labelScale : scale;
    EmitShadowIfAny(renderQueue, text, activeTextColor, texture, renderLayer, sortY, x, y, w, h, shadowScale, clipId,
                    snapShadow);
    if (hasUiRect) {
      const float uiOutlineW =
          text.outlineWidth > 0.0f ? std::max(1.0f, std::round(text.outlineWidth * labelScale)) : 0.0f;
      EmitOutlineRings(renderQueue, text, activeTextColor, texture, renderLayer, sortY, x, y, w, h, uiOutlineW, 8.0f,
                       1.0f, clipId);
    } else {
      EmitOutlineIfAny(renderQueue, text, activeTextColor, texture, renderLayer, sortY, x, y, w, h, scale, clipId);
    }
  }

  static void EmitOutlineRings(RenderQueue& renderQueue, const TextLabelComponent& text,
                               const octarine::Color& activeTextColor, SDL_Texture* texture, const int renderLayer,
                               const float sortY, const float x, const float y, const float w, const float h,
                               const float outlineWidth, const float maxOutlineWidth, const float scale,
                               const std::uint8_t clipId) {
    if (outlineWidth <= 0.0f || text.outlineColor.a == 0 || activeTextColor.a == 0) {
      return;
    }
    const auto outlineAlpha = static_cast<Uint8>((static_cast<int>(text.outlineColor.a) * activeTextColor.a) / 255);
    if (outlineAlpha == 0) return;
    const SDL_Color modColor{text.outlineColor.r, text.outlineColor.g, text.outlineColor.b, outlineAlpha};
    struct Offset {
      float x;
      float y;
    };
    static constexpr Offset kDirs[8] = {
        {-1.0f, -1.0f}, {0.0f, -1.0f}, {1.0f, -1.0f}, {-1.0f, 0.0f},
        {1.0f, 0.0f},   {-1.0f, 1.0f}, {0.0f, 1.0f},  {1.0f, 1.0f},
    };
    const float clampedWidth = std::min(outlineWidth, maxOutlineWidth);
    const int maxRing = std::clamp(static_cast<int>(std::ceil(clampedWidth)), 1, 8);
    for (int ring = 1; ring <= maxRing; ++ring) {
      const float radius = (ring == maxRing ? clampedWidth : static_cast<float>(ring)) * scale;
      for (const auto& dir : kDirs) {
        auto& outCmd =
            renderQueue.EmplaceText(static_cast<unsigned int>(std::max(0, renderLayer)), sortY, texture, clipId);
        outCmd.destRect = {x + dir.x * radius, y + dir.y * radius, w, h};
        outCmd.texture = texture;
        outCmd.colorMod = modColor;
      }
    }
  }

  static int ResolveWrapWidth(Registry* registry, const Entity entity, const TextLabelComponent& text,
                              const bool hasUiRect, const float labelScale) {
    if (text.wrapWidth > 0) {
      return hasUiRect ? std::max(1, static_cast<int>(std::lround(static_cast<float>(text.wrapWidth) * labelScale)))
                       : text.wrapWidth;
    }
    if (!text.wrapToRect || !hasUiRect) {
      return 0;
    }
    return std::max(0, static_cast<int>(registry->GetComponent<UIRectComponent>(entity).Width()));
  }

  struct TextPlacement {
    glm::vec2 origin{};
    float sortY = 0.0F;
    bool effectivelyFixed = true;
    bool visible = true;
    int renderLayer = 0;
    std::uint8_t clipId = 0;
  };

  static TextPlacement ResolvePlacement(Registry* registry, const Entity entity, const TextLabelComponent& text,
                                        const float textWidth, const float textHeight, const float labelScale) {
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
               text.position * labelScale;
      sortY = rect.top;
      effectivelyFixed = true;
      renderLayer = rect.layer;
    } else if (registry->HasComponent<GlobalTransformComponent>(entity)) {
      const auto& transform = registry->GetComponent<GlobalTransformComponent>(entity);
      origin += transform.position;
      sortY = origin.y;
    }
    origin.x = std::round(origin.x);
    origin.y = std::round(origin.y);
    return {origin, sortY, effectivelyFixed, visible, renderLayer, clipId};
  }

  mutable TextRenderCache* text_render_cache_ = nullptr;
  mutable const AssetManager* asset_manager_ = nullptr;
  mutable const EngineContext* engine_ctx_ = nullptr;
  mutable RenderQueue* render_queue_ = nullptr;
  mutable const CameraComponent* camera_comp_ = nullptr;
  mutable const GameConfig* game_config_ = nullptr;

  static glm::vec2 ResolveScreenOrigin(const glm::vec2 origin, const octarine::Rect& camera, const float zoom,
                                       const bool effectivelyFixed, const bool pixelSharp) {
    const glm::vec2 screen =
        effectivelyFixed ? origin : glm::vec2{(origin.x - camera.x) * zoom, (origin.y - camera.y) * zoom};
    return pixelSharp ? glm::vec2{std::floor(screen.x + 0.5f), std::floor(screen.y + 0.5f)} : screen;
  }

  static float ResolveRasterScale(const float rawScale, const bool pixelSharp) {
    if (rawScale <= 1.01F) return 1.0F;
    if (pixelSharp) {
      return std::clamp(std::max(1.0F, std::round(rawScale)), 1.0F, 4.0F);
    }
    const float quantized = std::round(rawScale * 4.0F) * 0.25F;
    return std::clamp(quantized, 1.0F, 4.0F);
  }

  std::unordered_map<std::uint64_t, TextCacheEntry>::iterator ResolveOrRasterize(
      Entity entity, const TextLabelComponent& text, const AssetManager& assetManager, SDL_Renderer* sdlRenderer,
      std::uint64_t fontGen, int effectiveWrapWidth, float rasterScale, float labelScale, float deviceScale,
      bool pixelSharp) const {
    auto& cache = *text_render_cache_;
    auto it = cache.text_cache_.find(entity.id);
    const bool needsRaster = it == cache.text_cache_.end() || it->second.fontId != text.fontId ||
                             it->second.fontGen != fontGen || it->second.text != text.text ||
                             it->second.wrapWidth != effectiveWrapWidth || it->second.pixelSharp != pixelSharp ||
                             std::abs(it->second.rasterScale - rasterScale) > 0.005F ||
                             std::abs(it->second.labelScale - labelScale) > 0.005F;
    if (!needsRaster) {
      return it;
    }

    const ContentKeyView view{text.fontId,
                              fontGen,
                              effectiveWrapWidth,
                              octarine::ui::QuantizeFontScaleKey(rasterScale),
                              octarine::ui::QuantizeFontScaleKey(deviceScale),
                              pixelSharp,
                              text.text};
    std::shared_ptr<SharedTextTexture> shared;
    if (const auto l2It = cache.content_cache_.find(view); l2It != cache.content_cache_.end()) {
      cache.content_lru_.splice(cache.content_lru_.begin(), cache.content_lru_, l2It->second);
      shared = l2It->second->tex;
    } else {
      TTF_Font* font = assetManager.GetFont(text.fontId);
      if (font == nullptr) {
        return cache.text_cache_.end();
      }
      shared = RasterizeLabel(assetManager, sdlRenderer, font, text, effectiveWrapWidth, rasterScale, labelScale,
                              deviceScale, pixelSharp);
      if (!shared) {
        return cache.text_cache_.end();
      }
      if (cache.content_lru_.size() >= TextRenderCache::kMaxContentCacheEntries) {
        cache.EvictOldestContent();
      }
      ContentKey key{text.fontId,         fontGen,    effectiveWrapWidth, view.rasterScaleKey,
                     view.deviceScaleKey, pixelSharp, text.text};
      cache.content_lru_.push_front(ContentNode{std::move(key), shared});
      cache.content_cache_.emplace(&cache.content_lru_.front().key, cache.content_lru_.begin());
    }

    const float scaleFactor = shared->fromAtlas ? labelScale : (deviceScale > 0.0F ? (1.0F / deviceScale) : 1.0F);
    const float logicalW = shared->texW * scaleFactor;
    const float logicalH = shared->texH * scaleFactor;
    return StoreEntry(it, entity, text, fontGen, effectiveWrapWidth, rasterScale, labelScale, pixelSharp,
                      std::move(shared), logicalW, logicalH);
  }

  std::unordered_map<std::uint64_t, TextCacheEntry>::iterator StoreEntry(
      std::unordered_map<std::uint64_t, TextCacheEntry>::iterator it, Entity entity, const TextLabelComponent& text,
      std::uint64_t fontGen, int wrapWidth, float rasterScale, float labelScale, bool pixelSharp,
      std::shared_ptr<SharedTextTexture> shared, float width, float height) const {
    auto& cache = *text_render_cache_;
    if (it != cache.text_cache_.end()) {
      cache.RetireIfLast(it->second.shared);
      it->second = {text.fontId, text.text,  fontGen,           wrapWidth, rasterScale,
                    labelScale,  pixelSharp, std::move(shared), width,     height};
      return it;
    }
    return cache.text_cache_
        .emplace(entity.id, TextCacheEntry{text.fontId, text.text, fontGen, wrapWidth, rasterScale, labelScale,
                                           pixelSharp, std::move(shared), width, height})
        .first;
  }

  static SDL_Surface* RenderTtfSurface(TTF_Font* font, const std::string& str, const int wrapWidth,
                                       const float texelPerLogical) {
    const SDL_Color white{255, 255, 255, 255};
    const bool hasNewline = str.find('\n') != std::string::npos;
    const int scaledWrap =
        wrapWidth > 0 ? std::max(1, static_cast<int>(std::lround(static_cast<float>(wrapWidth) * texelPerLogical))) : 0;
    return (wrapWidth > 0 || hasNewline) ? TTF_RenderText_Blended_Wrapped(font, str.c_str(), 0, white, scaledWrap)
                                         : TTF_RenderText_Blended(font, str.c_str(), 0, white);
  }

  static std::shared_ptr<SharedTextTexture> RasterizeLabel(const AssetManager& assetManager, SDL_Renderer* sdlRenderer,
                                                           TTF_Font* font, const TextLabelComponent& text,
                                                           const int wrapWidth, const float rasterScale,
                                                           const float labelScale, const float texelPerLogical,
                                                           const bool pixelSharp) {
    const SDL_Color white{255, 255, 255, 255};
    SDL_Surface* surface = nullptr;
    const GlyphAtlas* atlas = assetManager.GetGlyphAtlas(text.fontId);
    const bool hasNewline = text.text.find('\n') != std::string::npos;
    const int intLabelScale = octarine::ui::ResolveIntegerAtlasScale(labelScale);
    const bool allowAtlas = (std::abs(rasterScale - 1.0F) <= 0.01F) || (pixelSharp && intLabelScale > 0);
    bool fromAtlas = false;
    if (allowAtlas && wrapWidth <= 0 && !hasNewline && atlas != nullptr && atlas->IsLoaded()) {
      surface = ComposeFromAtlas(*atlas, text.text, white);
      fromAtlas = (surface != nullptr);
    }
    if (surface == nullptr) {
      TTF_Font* renderFont = assetManager.GetFontScaled(text.fontId, rasterScale);
      if (renderFont == nullptr) {
        renderFont = font;
      }
      surface = RenderTtfSurface(renderFont, text.text, wrapWidth, texelPerLogical);
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
    SDL_SetTextureScaleMode(texture, pixelSharp ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
    auto result = std::make_shared<SharedTextTexture>();
    result->ptr = texture;
    result->fromAtlas = fromAtlas;
    SDL_GetTextureSize(texture, &result->texW, &result->texH);
    return result;
  }

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

    SDL_SetSurfaceColorMod(src, 255, 255, 255);
    SDL_SetSurfaceAlphaMod(src, 255);
    return dst;
  }
};
