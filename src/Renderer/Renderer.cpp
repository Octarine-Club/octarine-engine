#include "Renderer/Renderer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#include <objc/message.h>
#include <objc/runtime.h>
#endif
#endif

#include "General/AngleUnit.h"
#include "General/Constants.h"
#include "General/Logger.h"
#include "General/Rotation2D.h"

namespace {
constexpr Uint8 kSceneClearGrey = 24;
constexpr Uint8 kWindowClearBlack = 0;
constexpr int kMaxRenderTargetDim = 8192;
constexpr float kHairlineScaleThreshold = 1.05F;

#if defined(__APPLE__) && TARGET_OS_OSX
void ConfigureMacMetalLayerVSync(SDL_Renderer* sdlRenderer, const bool enabled) {
  if (sdlRenderer == nullptr) return;
  void* layer = SDL_GetRenderMetalLayer(sdlRenderer);
  if (layer == nullptr) return;
  // Disable direct WindowServer scanout compositing when VSync is off so WindowServer does not
  // pin two of the three CAMetalLayer drawables across display refresh intervals on lower-Hz monitors.
  reinterpret_cast<void (*)(void*, SEL, bool)>(objc_msgSend)(layer, sel_registerName("setAllowsDisplayCompositing:"),
                                                             enabled);
}

// When SDL_SetRenderVSync(0) is toggled on macOS Metal, QuartzCore's CAImageQueue can leave
// previously queued IOSurface drawables in state 0 (queued for display scanout) when switching
// away from direct display compositing, starving nextDrawable into a 1Hz timeout.
// Probing the CAImageQueue shared-memory ring (validated against version 20 and capacity [4, 8])
// and atomically marking non-latest queued slots as collectable (0x22222222) releases those
// pinned drawables immediately; if any header check mismatches on a future macOS release, this
// function safely no-ops.
void ReclaimStaleMacMetalDrawables(SDL_Renderer* sdlRenderer) {
  if (sdlRenderer == nullptr) return;
  int vsync = 1;
  if (!SDL_GetRenderVSync(sdlRenderer, &vsync) || vsync != 0) return;
  void* layer = SDL_GetRenderMetalLayer(sdlRenderer);
  if (layer == nullptr) return;
  reinterpret_cast<void (*)(void*, SEL, bool)>(objc_msgSend)(layer, sel_registerName("setAllowsDisplayCompositing:"),
                                                             false);

  constexpr ptrdiff_t kLayerPrivOffset = 0x20;
  constexpr ptrdiff_t kPrivImageQueueOffset = 0x10;
  constexpr ptrdiff_t kQueueCapOffset = 0x28;
  constexpr ptrdiff_t kQueueShmHolderOffset = 0x30;
  constexpr ptrdiff_t kShmPtrOffset = 0x18;
  constexpr ptrdiff_t kShmCapOffset = 0x20;
  constexpr ptrdiff_t kShmLatestGenOffset = 0x24;
  constexpr ptrdiff_t kShmSlotsOffset = 0x1e0;
  constexpr ptrdiff_t kShmSlotStride = 0xa0;
  constexpr ptrdiff_t kSlotStateOffset = 0x3c;
  constexpr ptrdiff_t kSlotTypeOffset = 0x44;
  constexpr ptrdiff_t kSlotGenOffset = 0x64;
  constexpr uint32_t kExpectedShmVersion = 20U;
  constexpr uint32_t kMinQueueCap = 4U;
  constexpr uint32_t kMaxQueueCap = 8U;
  constexpr uint32_t kIOSurfaceSlotType = 3U;
  constexpr uint32_t kSlotStateQueued = 0U;
  constexpr uint32_t kSlotStateCollectable = 0x22222222U;

  const auto* priv = *reinterpret_cast<const char* const*>(static_cast<const char*>(layer) + kLayerPrivOffset);
  if (priv == nullptr) return;
  const auto* iq = *reinterpret_cast<const char* const*>(priv + kPrivImageQueueOffset);
  if (iq == nullptr) return;
  const uint32_t cap = *reinterpret_cast<const uint32_t*>(iq + kQueueCapOffset);
  const auto* shmHolder = *reinterpret_cast<const char* const*>(iq + kQueueShmHolderOffset);
  if (shmHolder == nullptr || cap < kMinQueueCap || cap > kMaxQueueCap) return;
  auto* shm = *reinterpret_cast<char* const*>(shmHolder + kShmPtrOffset);
  if (shm == nullptr) return;
  if (*reinterpret_cast<const uint32_t*>(shm) != kExpectedShmVersion ||
      *reinterpret_cast<const uint32_t*>(shm + kShmCapOffset) != cap) {
    return;
  }

  const uint32_t latestGen = *reinterpret_cast<const volatile uint32_t*>(shm + kShmLatestGenOffset);
  for (uint32_t si = 0; si < cap; ++si) {
    char* slot = shm + kShmSlotsOffset + static_cast<ptrdiff_t>(si) * kShmSlotStride;
    const uint32_t tp = *reinterpret_cast<const volatile uint32_t*>(slot + kSlotTypeOffset);
    const uint32_t gen = *reinterpret_cast<const volatile uint32_t*>(slot + kSlotGenOffset);
    if (tp == kIOSurfaceSlotType && gen != 0U && gen != latestGen) {
      auto* statePtr = reinterpret_cast<std::atomic<uint32_t>*>(slot + kSlotStateOffset);
      uint32_t expected = kSlotStateQueued;
      statePtr->compare_exchange_strong(expected, kSlotStateCollectable, std::memory_order_acq_rel);
    }
  }
}
#endif
}  // namespace

bool Renderer::CreateScene(SDL_Renderer* sdlRenderer, const int width, const int height) {
  if (sdlRenderer == nullptr) return false;
  logical_width_ = std::max(1, width);
  logical_height_ = std::max(1, height);
  DestroyScene();
  return EnsureSceneTargetSize(sdlRenderer, logical_width_, logical_height_);
}

bool Renderer::EnsureSceneTargetSize(SDL_Renderer* sdlRenderer, const int targetPixelW, const int targetPixelH) {
  if (sdlRenderer == nullptr) return false;
  const int clampedW = std::clamp(targetPixelW, 1, kMaxRenderTargetDim);
  const int clampedH = std::clamp(targetPixelH, 1, kMaxRenderTargetDim);
  if (scene_texture_ != nullptr && clampedW == target_width_ && clampedH == target_height_) {
    return true;
  }
  SDL_Texture* newTex =
      SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, clampedW, clampedH);
  if (newTex == nullptr) {
    Logger::Error("Renderer::EnsureSceneTargetSize SDL_CreateTexture failed: " + std::string(SDL_GetError()));
    return scene_texture_ != nullptr;
  }
  SDL_SetTextureScaleMode(newTex, SDL_SCALEMODE_NEAREST);
  SDL_SetTextureBlendMode(newTex, SDL_BLENDMODE_NONE);
  if (scene_texture_ != nullptr) {
    SDL_DestroyTexture(scene_texture_);
  }
  scene_texture_ = newTex;
  target_width_ = clampedW;
  target_height_ = clampedH;
  render_scale_x_ = logical_width_ > 0 ? static_cast<float>(target_width_) / static_cast<float>(logical_width_) : 1.0F;
  render_scale_y_ =
      logical_height_ > 0 ? static_cast<float>(target_height_) / static_cast<float>(logical_height_) : 1.0F;
  return true;
}

void Renderer::SetLogicalSize(const int logicalW, const int logicalH) {
  logical_width_ = std::max(1, logicalW);
  logical_height_ = std::max(1, logicalH);
  render_scale_x_ = (target_width_ > 0 && logical_width_ > 0)
                        ? static_cast<float>(target_width_) / static_cast<float>(logical_width_)
                        : 1.0F;
  render_scale_y_ = (target_height_ > 0 && logical_height_ > 0)
                        ? static_cast<float>(target_height_) / static_cast<float>(logical_height_)
                        : 1.0F;
}

void Renderer::DestroyScene() {
  if (scene_texture_ != nullptr) {
    SDL_DestroyTexture(scene_texture_);
    scene_texture_ = nullptr;
  }
  target_width_ = 0;
  target_height_ = 0;
  render_scale_x_ = 1.0F;
  render_scale_y_ = 1.0F;
}

void Renderer::BeginScene(SDL_Renderer* sdlRenderer) const {
  SDL_SetRenderTarget(sdlRenderer, scene_texture_);
  SDL_SetRenderScale(sdlRenderer, render_scale_x_, render_scale_y_);
  SDL_SetRenderDrawColor(sdlRenderer, kSceneClearGrey, kSceneClearGrey, kSceneClearGrey, Constants::kUint8Max);
  SDL_RenderClear(sdlRenderer);
}

void Renderer::EndScene(SDL_Renderer* sdlRenderer, const bool clearBackbuffer) const {
#if defined(__APPLE__) && TARGET_OS_OSX
  SDL_FlushRenderer(sdlRenderer);
  ReclaimStaleMacMetalDrawables(sdlRenderer);
#endif
  SDL_SetRenderScale(sdlRenderer, 1.0F, 1.0F);
  SDL_SetRenderTarget(sdlRenderer, nullptr);
  SDL_SetRenderScale(sdlRenderer, 1.0F, 1.0F);
  if (clearBackbuffer) {
    SDL_SetRenderDrawColor(sdlRenderer, kWindowClearBlack, kWindowClearBlack, kWindowClearBlack, Constants::kUint8Max);
    SDL_RenderClear(sdlRenderer);
  }
}

void Renderer::CompositeSceneToWindow(SDL_Renderer* sdlRenderer, const SDL_FRect* destRect) const {
  if (scene_texture_ != nullptr) {
    SDL_RenderTexture(sdlRenderer, scene_texture_, nullptr, destRect);
  }
}

SDL_FRect Renderer::ComputeCompositeRect(const int sceneW, const int sceneH, const int windowW, const int windowH,
                                         const PresentationMode mode) {
  if (windowW <= 0 || windowH <= 0) {
    return {0.0F, 0.0F, static_cast<float>(std::max(0, sceneW)), static_cast<float>(std::max(0, sceneH))};
  }
  if (sceneW <= 0 || sceneH <= 0 || mode == PresentationMode::Stretch) {
    return {0.0F, 0.0F, static_cast<float>(windowW), static_cast<float>(windowH)};
  }

  if ((mode == PresentationMode::IntegerScale || mode == PresentationMode::Expand) && windowW >= sceneW &&
      windowH >= sceneH) {
    const int scale = std::max(1, std::min(windowW / sceneW, windowH / sceneH));
    const auto w = static_cast<float>(sceneW * scale);
    const auto h = static_cast<float>(sceneH * scale);
    const float x = std::round((static_cast<float>(windowW) - w) * 0.5F);
    const float y = std::round((static_cast<float>(windowH) - h) * 0.5F);
    return {x, y, w, h};
  }

  const float scaleX = static_cast<float>(windowW) / static_cast<float>(sceneW);
  const float scaleY = static_cast<float>(windowH) / static_cast<float>(sceneH);
  const float scale = std::min(scaleX, scaleY);
  const auto w = static_cast<float>(std::max(1, static_cast<int>(std::lround(static_cast<float>(sceneW) * scale))));
  const auto h = static_cast<float>(std::max(1, static_cast<int>(std::lround(static_cast<float>(sceneH) * scale))));
  const float x = std::round((static_cast<float>(windowW) - w) * 0.5F);
  const float y = std::round((static_cast<float>(windowH) - h) * 0.5F);
  return {x, y, w, h};
}

namespace {
inline int RoundToEven(const float v) {
  const int n = static_cast<int>(std::lround(v));
  return (n % 2 != 0) ? (n + 1) : n;
}
}  // namespace

std::pair<int, int> Renderer::ComputeLogicalSceneSize(const int baseW, const int baseH, const int windowW,
                                                      const int windowH, const PresentationMode mode,
                                                      const float minAspect, const float maxAspect) {
  if (baseW <= 0 || baseH <= 0) {
    return {std::max(1, baseW), std::max(1, baseH)};
  }
  if (mode != PresentationMode::Expand || windowW <= 0 || windowH <= 0) {
    return {baseW, baseH};
  }

  float targetAspect = static_cast<float>(windowW) / static_cast<float>(windowH);
  int availW = windowW;
  int availH = windowH;
  if (minAspect > 0.0F && targetAspect < minAspect) {
    targetAspect = minAspect;
    availH = std::clamp(static_cast<int>(std::lround(static_cast<float>(windowW) / minAspect)), 1, windowH);
  }
  if (maxAspect > 0.0F && targetAspect > maxAspect) {
    targetAspect = maxAspect;
    availW = std::clamp(static_cast<int>(std::lround(static_cast<float>(windowH) * maxAspect)), 1, windowW);
  }

  if (availW >= baseW && availH >= baseH) {
    const int k = std::max(1, std::min(availW / baseW, availH / baseH));
    return {std::max(1, availW / k), std::max(1, availH / k)};
  }

  const float baseAspect = static_cast<float>(baseW) / static_cast<float>(baseH);
  int logicalW = baseW;
  int logicalH = baseH;
  if (targetAspect >= baseAspect) {
    logicalH = baseH;
    logicalW = RoundToEven(static_cast<float>(baseH) * targetAspect);
  } else {
    logicalW = baseW;
    logicalH = RoundToEven(static_cast<float>(baseW) / targetAspect);
  }
  return {std::max(1, logicalW), std::max(1, logicalH)};
}

void Renderer::Present(SDL_Renderer* sdlRenderer) const {
  SDL_RenderPresent(sdlRenderer);
#if defined(__APPLE__) && TARGET_OS_OSX
  ReclaimStaleMacMetalDrawables(sdlRenderer);
#endif
}

void Renderer::ApplyVSync(SDL_Renderer* sdlRenderer, const bool enabled) {
  if (sdlRenderer == nullptr) return;
  SDL_SetRenderVSync(sdlRenderer, enabled ? 1 : 0);
#if defined(__APPLE__) && TARGET_OS_OSX
  ConfigureMacMetalLayerVSync(sdlRenderer, enabled);
#endif
}

bool Renderer::CaptureScene(SDL_Renderer* sdlRenderer, const std::string& path) const {
  if (scene_texture_ == nullptr) {
    Logger::Error("Renderer::CaptureScene: no scene texture to capture");
    return false;
  }
  // Read back from the scene target (the off-screen texture the frame was drawn into). In bench
  // mode the window backbuffer is never composited, so the scene texture is the only place the
  // rendered frame lives.
  SDL_SetRenderTarget(sdlRenderer, scene_texture_);
  SDL_Surface* surface = SDL_RenderReadPixels(sdlRenderer, nullptr);
  SDL_SetRenderTarget(sdlRenderer, nullptr);
  if (surface == nullptr) {
    Logger::Error("Renderer::CaptureScene SDL_RenderReadPixels failed: " + std::string(SDL_GetError()));
    return false;
  }
  const bool ok = SDL_SaveBMP(surface, path.c_str());
  if (!ok) {
    Logger::Error("Renderer::CaptureScene SDL_SaveBMP failed: " + std::string(SDL_GetError()));
  }
  SDL_DestroySurface(surface);
  return ok;
}

namespace {

struct DrawStateCache {
  struct TexEntry {
    SDL_Texture* tex = nullptr;
    Uint8 r = 255;
    Uint8 g = 255;
    Uint8 b = 255;
    Uint8 a = 255;
    SDL_BlendMode blend = SDL_BLENDMODE_INVALID;
  };

  static constexpr size_t kMaxTexCache = 8;
  TexEntry texCache[kMaxTexCache]{};
  size_t texCount = 0;
  size_t texClock = 0;
  SDL_BlendMode drawBlend = SDL_BLENDMODE_INVALID;
  bool hasDrawColor = false;
  SDL_Color drawColor{};

  void ApplyTextureState(SDL_Renderer* /*renderer*/, SDL_Texture* tex, const SDL_Color& col,
                         const SDL_BlendMode blend) {
    TexEntry* slot = nullptr;
    for (size_t i = 0; i < texCount; ++i) {
      if (texCache[i].tex == tex) {
        slot = &texCache[i];
        break;
      }
    }
    if (slot == nullptr) {
      if (texCount < kMaxTexCache) {
        slot = &texCache[texCount++];
      } else {
        slot = &texCache[(texClock++) % kMaxTexCache];
      }
      slot->tex = tex;
      SDL_SetTextureColorMod(tex, col.r, col.g, col.b);
      SDL_SetTextureAlphaMod(tex, col.a);
      SDL_SetTextureBlendMode(tex, blend);
      slot->r = col.r;
      slot->g = col.g;
      slot->b = col.b;
      slot->a = col.a;
      slot->blend = blend;
      return;
    }
    if (slot->r != col.r || slot->g != col.g || slot->b != col.b) {
      SDL_SetTextureColorMod(tex, col.r, col.g, col.b);
      slot->r = col.r;
      slot->g = col.g;
      slot->b = col.b;
    }
    if (slot->a != col.a) {
      SDL_SetTextureAlphaMod(tex, col.a);
      slot->a = col.a;
    }
    if (slot->blend != blend) {
      SDL_SetTextureBlendMode(tex, blend);
      slot->blend = blend;
    }
  }

  void ApplyDrawBlend(SDL_Renderer* renderer, const SDL_BlendMode blend) {
    if (drawBlend != blend) {
      SDL_SetRenderDrawBlendMode(renderer, blend);
      drawBlend = blend;
    }
  }

  void ApplyDrawColor(SDL_Renderer* renderer, const SDL_Color& col) {
    if (!hasDrawColor || drawColor.r != col.r || drawColor.g != col.g || drawColor.b != col.b || drawColor.a != col.a) {
      SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
      drawColor = col;
      hasDrawColor = true;
    }
  }
};

inline SDL_FColor ToSdlFColor(const SDL_Color& c) {
  return {
      static_cast<float>(c.r) / 255.0f,
      static_cast<float>(c.g) / 255.0f,
      static_cast<float>(c.b) / 255.0f,
      static_cast<float>(c.a) / 255.0f,
  };
}

void DrawSprite(SDL_Renderer* renderer, DrawStateCache& state, const SpriteCommand& cmd) {
  const SDL_FRect destRect = {cmd.destX, cmd.destY, cmd.destW, cmd.destH};
  const SDL_FRect* srcRectPtr = (cmd.srcRect.w > 0.0f && cmd.srcRect.h > 0.0f) ? &cmd.srcRect : nullptr;
  state.ApplyTextureState(renderer, cmd.texture, cmd.colorMod, cmd.blendMode);
  if (cmd.rotation == 0.0F && cmd.flip == SDL_FLIP_NONE) {
    SDL_RenderTexture(renderer, cmd.texture, srcRectPtr, &destRect);
    return;
  }
  const float deg = cmd.rotation * octarine::kRadiansToDegrees;
  SDL_RenderTextureRotated(renderer, cmd.texture, srcRectPtr, &destRect, deg, &cmd.pivot, cmd.flip);
}

void DrawText(SDL_Renderer* renderer, DrawStateCache& state, const TextCommand& cmd) {
  state.ApplyTextureState(renderer, cmd.texture, cmd.colorMod, SDL_BLENDMODE_BLEND);
  SDL_RenderTexture(renderer, cmd.texture, nullptr, &cmd.destRect);
}

void DrawTileChunk(SDL_Renderer* renderer, DrawStateCache& state, const TileChunkCommand& cmd) {
  if (cmd.vertices == nullptr || cmd.vertexCount <= 0) {
    return;
  }
  if (cmd.texture != nullptr) {
    state.ApplyTextureState(renderer, cmd.texture, SDL_Color{255, 255, 255, 255}, cmd.blendMode);
  } else {
    state.ApplyDrawBlend(renderer, cmd.blendMode);
  }
  SDL_RenderGeometry(renderer, cmd.texture, cmd.vertices, cmd.vertexCount, cmd.indices, cmd.indexCount);
}

void BuildCapsuleContour(const glm::vec2 p1, const glm::vec2 p2, const glm::vec2 dir, const int arcSegs,
                         const octarine::Rotation2D stepRot, glm::vec2* outCenters, glm::vec2* outNormals) {
  glm::vec2 unitDir{dir.y, -dir.x};
  for (int i = 0; i <= arcSegs; ++i) {
    outCenters[i] = p2;
    outNormals[i] = unitDir;
    unitDir = octarine::Rotate(unitDir, stepRot);
  }
  unitDir = {-dir.y, dir.x};
  for (int i = 0; i <= arcSegs; ++i) {
    outCenters[arcSegs + 1 + i] = p1;
    outNormals[arcSegs + 1 + i] = unitDir;
    unitDir = octarine::Rotate(unitDir, stepRot);
  }
}

class GeometryBatcher {
 public:
  GeometryBatcher(std::vector<SDL_Vertex>& verts, std::vector<int>& indices, DrawStateCache& state)
      : verts_(verts), indices_(indices), state_(state) {
    verts_.clear();
    indices_.clear();
  }

  void PushSprite(SDL_Renderer* renderer, const SpriteCommand& cmd) {
    if (cmd.texture == nullptr) return;
    if (kind_ != BatchKind::Sprite || batch_tex_ != cmd.texture || batch_blend_ != cmd.blendMode ||
        verts_.size() + 4 > kMaxBatchVerts) {
      Flush(renderer);
      if (!ResolveTextureInvSize(cmd.texture)) {
        DrawSprite(renderer, state_, cmd);
        return;
      }
      kind_ = BatchKind::Sprite;
      batch_tex_ = cmd.texture;
      batch_blend_ = cmd.blendMode;
    }

    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    if (cmd.srcRect.w > 0.0f && cmd.srcRect.h > 0.0f) {
      u0 = cmd.srcRect.x * inv_tex_w_;
      v0 = cmd.srcRect.y * inv_tex_h_;
      u1 = (cmd.srcRect.x + cmd.srcRect.w) * inv_tex_w_;
      v1 = (cmd.srcRect.y + cmd.srcRect.h) * inv_tex_h_;
    }
    const auto flipBits = static_cast<unsigned int>(cmd.flip);
    if ((flipBits & static_cast<unsigned int>(SDL_FLIP_HORIZONTAL)) != 0U) std::swap(u0, u1);
    if ((flipBits & static_cast<unsigned int>(SDL_FLIP_VERTICAL)) != 0U) std::swap(v0, v1);

    const SDL_FColor fcol = ToSdlFColor(cmd.colorMod);
    AppendTransformedQuad(cmd.destX, cmd.destY, cmd.destW, cmd.destH, cmd.pivot, cmd.rotation, fcol, u0, v0, u1, v1);
  }

  void PushSquare(SDL_Renderer* renderer, const SquareCommand& cmd) {
    EnsureUntextured(renderer, cmd.blendMode, 4);
    const SDL_FColor fcol = ToSdlFColor(cmd.color);
    AppendTransformedQuad(cmd.destRect.x, cmd.destRect.y, cmd.destRect.w, cmd.destRect.h, cmd.pivot, cmd.rotation, fcol,
                          0.0f, 0.0f, 0.0f, 0.0f);
  }

  void PushLine(SDL_Renderer* renderer, const LineCommand& cmd, const float renderScale) {
    const float effectiveThickness = std::max(cmd.thickness, 1.0F);
    if (effectiveThickness * renderScale <= kHairlineScaleThreshold) {
      Flush(renderer);
      state_.ApplyDrawBlend(renderer, cmd.blendMode);
      state_.ApplyDrawColor(renderer, cmd.color);
      SDL_RenderLine(renderer, cmd.start.x, cmd.start.y, cmd.end.x, cmd.end.y);
      return;
    }
    const glm::vec2 d{cmd.end.x - cmd.start.x, cmd.end.y - cmd.start.y};
    const float len = glm::length(d);
    if (len < 0.0001f) return;

    EnsureUntextured(renderer, cmd.blendMode, 4);
    const glm::vec2 n = glm::vec2{-d.y, d.x} * (effectiveThickness * 0.5f / len);
    const SDL_FColor fcol = ToSdlFColor(cmd.color);
    AppendQuad({cmd.start.x + n.x, cmd.start.y + n.y}, {cmd.start.x - n.x, cmd.start.y - n.y},
               {cmd.end.x - n.x, cmd.end.y - n.y}, {cmd.end.x + n.x, cmd.end.y + n.y}, fcol, 0.0f, 0.0f, 0.0f, 0.0f);
  }

  void PushCircle(SDL_Renderer* renderer, const CircleCommand& cmd, const float renderScale) {
    if (cmd.radiusX <= 0.0f || cmd.radiusY <= 0.0f) return;

    constexpr int kMinSegments = 16;
    const float maxR = std::max(cmd.radiusX, cmd.radiusY) * std::max(1.0F, renderScale);
    const int segments = std::clamp(static_cast<int>(maxR * 0.75f), kMinSegments, kMaxCircleSegments);
    const float step = (2.0f * std::numbers::pi_v<float>) / static_cast<float>(segments);
    const auto stepRot = octarine::Rotation2D::FromRadians(step);
    const bool rotateEllipse = (cmd.rotation != 0.0f) && (cmd.radiusX != cmd.radiusY);
    const auto rot = rotateEllipse ? octarine::Rotation2D::FromRadians(cmd.rotation) : octarine::Rotation2D{};

    if (cmd.filled) {
      PushFilledCircle(renderer, cmd, segments, stepRot, rotateEllipse, rot);
    } else {
      PushOutlineCircle(renderer, cmd, segments, stepRot, rotateEllipse, rot, renderScale);
    }
  }

  void PushTriangle(SDL_Renderer* renderer, const TriangleCommand& cmd, const float renderScale) {
    if (cmd.filled) {
      EnsureUntextured(renderer, cmd.blendMode, 3);
      const SDL_FColor fcol = ToSdlFColor(cmd.color);
      const int base = static_cast<int>(verts_.size());
      verts_.push_back({cmd.p1, fcol, {0.0f, 0.0f}});
      verts_.push_back({cmd.p2, fcol, {0.0f, 0.0f}});
      verts_.push_back({cmd.p3, fcol, {0.0f, 0.0f}});
      indices_.push_back(base);
      indices_.push_back(base + 1);
      indices_.push_back(base + 2);
      return;
    }

    const float effectiveThickness = std::max(cmd.thickness, 1.0F);
    const glm::vec2 v[3] = {{cmd.p1.x, cmd.p1.y}, {cmd.p2.x, cmd.p2.y}, {cmd.p3.x, cmd.p3.y}};
    const float twiceArea = (v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[1].y - v[0].y) * (v[2].x - v[0].x);
    if (effectiveThickness * renderScale <= kHairlineScaleThreshold || std::abs(twiceArea) < 0.0001f) {
      Flush(renderer);
      state_.ApplyDrawBlend(renderer, cmd.blendMode);
      state_.ApplyDrawColor(renderer, cmd.color);
      const SDL_FPoint points[4] = {cmd.p1, cmd.p2, cmd.p3, cmd.p1};
      SDL_RenderLines(renderer, points, 4);
      return;
    }

    const float sign = (twiceArea >= 0.0f) ? 1.0f : -1.0f;
    glm::vec2 normals[3];
    for (int i = 0; i < 3; ++i) {
      const glm::vec2 edge = v[(i + 1) % 3] - v[i];
      const float len = glm::length(edge);
      normals[i] = (len > 0.0001f) ? (sign * glm::vec2{edge.y, -edge.x} / len) : glm::vec2{0.0f, 0.0f};
    }
    SDL_FPoint outerPts[3];
    SDL_FPoint innerPts[3];
    for (int i = 0; i < 3; ++i) {
      const glm::vec2 b = normals[(i + 2) % 3] + normals[i];
      // For unit edge normals, dot(b, n_i) == 0.5 * dot(b, b); clamping dot(b, b) >= 0.5 caps sharp miters.
      const glm::vec2 miter = b * (effectiveThickness / std::max(0.5f, glm::dot(b, b)));
      outerPts[i] = {v[i].x + miter.x, v[i].y + miter.y};
      innerPts[i] = {v[i].x - miter.x, v[i].y - miter.y};
    }
    EnsureUntextured(renderer, cmd.blendMode, 6);
    AppendRingStrip(ToSdlFColor(cmd.color), outerPts, innerPts, 3);
  }

  void PushCapsule(SDL_Renderer* renderer, const CapsuleCommand& cmd, const float renderScale) {
    if (cmd.radius <= 0.0f) {
      PushLine(renderer, LineCommand{cmd.start, cmd.end, cmd.thickness, cmd.color, cmd.blendMode}, renderScale);
      return;
    }
    const glm::vec2 p1{cmd.start.x, cmd.start.y};
    const glm::vec2 p2{cmd.end.x, cmd.end.y};
    const glm::vec2 d = p2 - p1;
    const float len = glm::length(d);
    if (len < 0.0001f) {
      PushCircle(
          renderer,
          CircleCommand{cmd.start, cmd.radius, cmd.radius, 0.0f, cmd.thickness, cmd.color, cmd.blendMode, cmd.filled},
          renderScale);
      return;
    }

    const auto stepRot =
        octarine::Rotation2D::FromRadians(std::numbers::pi_v<float> / static_cast<float>(kCapsuleArcSegs));
    glm::vec2 centers[kMaxCapsuleVerts];
    glm::vec2 normals[kMaxCapsuleVerts];
    BuildCapsuleContour(p1, p2, d / len, kCapsuleArcSegs, stepRot, centers, normals);

    if (!cmd.filled) {
      PushOutlineCapsule(renderer, cmd, centers, normals, kMaxCapsuleVerts, renderScale);
      return;
    }

    SDL_FPoint ringPts[kMaxCapsuleVerts];
    size_t idx = 0;
    for (auto& rp : ringPts) {
      const glm::vec2 pt = centers[idx] + cmd.radius * normals[idx];
      rp = {pt.x, pt.y};
      ++idx;
    }
    const glm::vec2 midpoint = (p1 + p2) * 0.5f;
    EnsureUntextured(renderer, cmd.blendMode, static_cast<size_t>(kMaxCapsuleVerts) + 1u);
    AppendFan(ToSdlFColor(cmd.color), {midpoint.x, midpoint.y}, ringPts, kMaxCapsuleVerts);
  }

  void Flush(SDL_Renderer* renderer) {
    if (verts_.empty()) return;
    if (kind_ == BatchKind::Sprite) {
      state_.ApplyTextureState(renderer, batch_tex_, SDL_Color{255, 255, 255, 255}, batch_blend_);
      SDL_RenderGeometry(renderer, batch_tex_, verts_.data(), static_cast<int>(verts_.size()), indices_.data(),
                         static_cast<int>(indices_.size()));
    } else if (kind_ == BatchKind::Untextured) {
      state_.ApplyDrawBlend(renderer, batch_blend_);
      SDL_RenderGeometry(renderer, nullptr, verts_.data(), static_cast<int>(verts_.size()), indices_.data(),
                         static_cast<int>(indices_.size()));
    }
    verts_.clear();
    indices_.clear();
    kind_ = BatchKind::None;
  }

 private:
  static constexpr size_t kMaxBatchVerts = 60000;
  static constexpr int kMaxCircleSegments = 64;
  static constexpr int kCapsuleArcSegs = 16;
  static constexpr int kMaxCapsuleVerts = (kCapsuleArcSegs + 1) * 2;
  enum class BatchKind : uint8_t { None, Sprite, Untextured };

  void AppendTransformedQuad(const float destX, const float destY, const float destW, const float destH,
                             const SDL_FPoint& pivot, const float rotation, const SDL_FColor& fcol, const float u0,
                             const float v0, const float u1, const float v1) {
    if (rotation == 0.0F) {
      const float x1 = destX + destW;
      const float y1 = destY + destH;
      AppendQuad({destX, destY}, {x1, destY}, {x1, y1}, {destX, y1}, fcol, u0, v0, u1, v1);
      return;
    }

    const glm::vec2 centre = {destX + pivot.x, destY + pivot.y};
    const auto rot = octarine::Rotation2D::FromRadians(rotation);
    const float lx0 = -pivot.x;
    const float ly0 = -pivot.y;
    const float lx1 = destW - pivot.x;
    const float ly1 = destH - pivot.y;
    const glm::vec2 p0 = centre + octarine::Rotate(glm::vec2{lx0, ly0}, rot);
    const glm::vec2 p1 = centre + octarine::Rotate(glm::vec2{lx1, ly0}, rot);
    const glm::vec2 p2 = centre + octarine::Rotate(glm::vec2{lx1, ly1}, rot);
    const glm::vec2 p3 = centre + octarine::Rotate(glm::vec2{lx0, ly1}, rot);
    AppendQuad({p0.x, p0.y}, {p1.x, p1.y}, {p2.x, p2.y}, {p3.x, p3.y}, fcol, u0, v0, u1, v1);
  }

  void EnsureUntextured(SDL_Renderer* renderer, const SDL_BlendMode blend, const size_t neededVerts) {
    if (kind_ != BatchKind::Untextured || batch_blend_ != blend || verts_.size() + neededVerts > kMaxBatchVerts) {
      Flush(renderer);
      kind_ = BatchKind::Untextured;
      batch_blend_ = blend;
    }
  }

  void PushFilledCircle(SDL_Renderer* renderer, const CircleCommand& cmd, const int segments,
                        const octarine::Rotation2D stepRot, const bool rotateEllipse, const octarine::Rotation2D rot) {
    SDL_FPoint ringPts[kMaxCircleSegments];
    glm::vec2 unitDir{1.0f, 0.0f};
    for (int i = 0; i < segments; ++i) {
      glm::vec2 offset{cmd.radiusX * unitDir.x, cmd.radiusY * unitDir.y};
      if (rotateEllipse) offset = octarine::Rotate(offset, rot);
      ringPts[i] = {cmd.center.x + offset.x, cmd.center.y + offset.y};
      unitDir = octarine::Rotate(unitDir, stepRot);
    }
    EnsureUntextured(renderer, cmd.blendMode, static_cast<size_t>(segments) + 1u);
    AppendFan(ToSdlFColor(cmd.color), cmd.center, ringPts, segments);
  }

  void PushOutlineCircle(SDL_Renderer* renderer, const CircleCommand& cmd, const int segments,
                         const octarine::Rotation2D stepRot, const bool rotateEllipse, const octarine::Rotation2D rot,
                         const float renderScale) {
    const float effectiveThickness = std::max(cmd.thickness, 1.0F);
    if (effectiveThickness * renderScale <= kHairlineScaleThreshold) {
      Flush(renderer);
      state_.ApplyDrawBlend(renderer, cmd.blendMode);
      state_.ApplyDrawColor(renderer, cmd.color);
      SDL_FPoint points[kMaxCircleSegments + 1];
      glm::vec2 unitDir{1.0f, 0.0f};
      for (int i = 0; i < segments; ++i) {
        glm::vec2 offset{cmd.radiusX * unitDir.x, cmd.radiusY * unitDir.y};
        if (rotateEllipse) offset = octarine::Rotate(offset, rot);
        points[i] = {cmd.center.x + offset.x, cmd.center.y + offset.y};
        unitDir = octarine::Rotate(unitDir, stepRot);
      }
      points[segments] = points[0];
      SDL_RenderLines(renderer, points, segments + 1);
      return;
    }

    const float halfT = effectiveThickness * 0.5f;
    const float outRx = cmd.radiusX + halfT;
    const float outRy = cmd.radiusY + halfT;
    const float inRx = std::max(0.0f, cmd.radiusX - halfT);
    const float inRy = std::max(0.0f, cmd.radiusY - halfT);
    SDL_FPoint outerPts[kMaxCircleSegments];
    SDL_FPoint innerPts[kMaxCircleSegments];
    glm::vec2 unitDir{1.0f, 0.0f};
    for (int i = 0; i < segments; ++i) {
      glm::vec2 outOff{outRx * unitDir.x, outRy * unitDir.y};
      glm::vec2 inOff{inRx * unitDir.x, inRy * unitDir.y};
      if (rotateEllipse) {
        outOff = octarine::Rotate(outOff, rot);
        inOff = octarine::Rotate(inOff, rot);
      }
      outerPts[i] = {cmd.center.x + outOff.x, cmd.center.y + outOff.y};
      innerPts[i] = {cmd.center.x + inOff.x, cmd.center.y + inOff.y};
      unitDir = octarine::Rotate(unitDir, stepRot);
    }
    EnsureUntextured(renderer, cmd.blendMode, static_cast<size_t>(segments) * 2u);
    AppendRingStrip(ToSdlFColor(cmd.color), outerPts, innerPts, segments);
  }

  void PushOutlineCapsule(SDL_Renderer* renderer, const CapsuleCommand& cmd, const glm::vec2* centers,
                          const glm::vec2* normals, const int totalVerts, const float renderScale) {
    const float effectiveThickness = std::max(cmd.thickness, 1.0F);
    if (effectiveThickness * renderScale <= kHairlineScaleThreshold) {
      Flush(renderer);
      state_.ApplyDrawBlend(renderer, cmd.blendMode);
      state_.ApplyDrawColor(renderer, cmd.color);
      SDL_FPoint points[kMaxCapsuleVerts + 1];
      for (int i = 0; i < totalVerts; ++i) {
        points[i] = {centers[i].x + cmd.radius * normals[i].x, centers[i].y + cmd.radius * normals[i].y};
      }
      points[totalVerts] = points[0];
      SDL_RenderLines(renderer, points, totalVerts + 1);
      return;
    }

    const float halfT = effectiveThickness * 0.5f;
    const float outR = cmd.radius + halfT;
    const float inR = std::max(0.0f, cmd.radius - halfT);
    SDL_FPoint outerPts[kMaxCapsuleVerts];
    SDL_FPoint innerPts[kMaxCapsuleVerts];
    for (int i = 0; i < totalVerts; ++i) {
      outerPts[i] = {centers[i].x + outR * normals[i].x, centers[i].y + outR * normals[i].y};
      innerPts[i] = {centers[i].x + inR * normals[i].x, centers[i].y + inR * normals[i].y};
    }
    EnsureUntextured(renderer, cmd.blendMode, static_cast<size_t>(totalVerts) * 2u);
    AppendRingStrip(ToSdlFColor(cmd.color), outerPts, innerPts, totalVerts);
  }

  bool ResolveTextureInvSize(SDL_Texture* tex) {
    if (tex == cached_size_tex_) {
      inv_tex_w_ = cached_inv_w_;
      inv_tex_h_ = cached_inv_h_;
      return cached_inv_w_ > 0.0f && cached_inv_h_ > 0.0f;
    }
    float w = 0.0f;
    float h = 0.0f;
    if (!SDL_GetTextureSize(tex, &w, &h) || w <= 0.0f || h <= 0.0f) {
      return false;
    }
    cached_size_tex_ = tex;
    cached_inv_w_ = 1.0f / w;
    cached_inv_h_ = 1.0f / h;
    inv_tex_w_ = cached_inv_w_;
    inv_tex_h_ = cached_inv_h_;
    return true;
  }

  void AppendQuad(const SDL_FPoint p0, const SDL_FPoint p1, const SDL_FPoint p2, const SDL_FPoint p3,
                  const SDL_FColor& col, const float u0, const float v0, const float u1, const float v1) {
    const int base = static_cast<int>(verts_.size());
    verts_.push_back({p0, col, {u0, v0}});
    verts_.push_back({p1, col, {u1, v0}});
    verts_.push_back({p2, col, {u1, v1}});
    verts_.push_back({p3, col, {u0, v1}});
    indices_.push_back(base);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);
  }

  void AppendFan(const SDL_FColor& col, const SDL_FPoint center, const SDL_FPoint* ringPts, const int count) {
    const int base = static_cast<int>(verts_.size());
    verts_.push_back({center, col, {0.0f, 0.0f}});
    for (int i = 0; i < count; ++i) {
      verts_.push_back({ringPts[i], col, {0.0f, 0.0f}});
    }
    for (int i = 0; i < count; ++i) {
      indices_.push_back(base);
      indices_.push_back(base + i + 1);
      indices_.push_back(base + ((i + 1 == count) ? 1 : (i + 2)));
    }
  }

  void AppendRingStrip(const SDL_FColor& col, const SDL_FPoint* outerPts, const SDL_FPoint* innerPts, const int count) {
    const int base = static_cast<int>(verts_.size());
    for (int i = 0; i < count; ++i) {
      verts_.push_back({outerPts[i], col, {0.0f, 0.0f}});
      verts_.push_back({innerPts[i], col, {0.0f, 0.0f}});
      const int next = (i + 1 == count) ? 0 : (i + 1);
      indices_.push_back(base + 2 * i);
      indices_.push_back(base + 2 * i + 1);
      indices_.push_back(base + 2 * next);
      indices_.push_back(base + 2 * next);
      indices_.push_back(base + 2 * i + 1);
      indices_.push_back(base + 2 * next + 1);
    }
  }

  std::vector<SDL_Vertex>& verts_;
  std::vector<int>& indices_;
  DrawStateCache& state_;
  BatchKind kind_ = BatchKind::None;
  SDL_Texture* batch_tex_ = nullptr;
  SDL_BlendMode batch_blend_ = SDL_BLENDMODE_NONE;
  float inv_tex_w_ = 0.0f;
  float inv_tex_h_ = 0.0f;
  SDL_Texture* cached_size_tex_ = nullptr;
  float cached_inv_w_ = 0.0f;
  float cached_inv_h_ = 0.0f;
};

}  // namespace

void Renderer::DrawQueue(const RenderQueue& renderQueue, SDL_Renderer* renderer) const {
  const float renderScale = std::max(render_scale_x_, render_scale_y_);
  DrawStateCache state{};
  GeometryBatcher batcher(batch_verts_, batch_indices_, state);
  std::uint8_t activeClipId = 0;
  for (const RenderKey& key : renderQueue) {
    if (key.clipId != activeClipId) {
      batcher.Flush(renderer);
      activeClipId = key.clipId;
      SDL_SetRenderClipRect(renderer, renderQueue.GetClipRect(activeClipId));
    }
    switch (key.type) {
      case SPRITE:
        batcher.PushSprite(renderer, key.payload.sprite);
        break;
      case SQUARE_PRIMITIVE:
        batcher.PushSquare(renderer, key.payload.square);
        break;
      case TEXT:
        batcher.Flush(renderer);
        DrawText(renderer, state, key.payload.text);
        break;
      case CIRCLE_PRIMITIVE:
        batcher.PushCircle(renderer, key.payload.circle, renderScale);
        break;
      case LINE_PRIMITIVE:
        batcher.PushLine(renderer, key.payload.line, renderScale);
        break;
      case TRIANGLE_PRIMITIVE:
        batcher.PushTriangle(renderer, key.payload.triangle, renderScale);
        break;
      case CAPSULE_PRIMITIVE:
        batcher.PushCapsule(renderer, key.payload.capsule, renderScale);
        break;
      case TILE_CHUNK:
        batcher.Flush(renderer);
        DrawTileChunk(renderer, state, key.payload.tileChunk);
        break;
      default:
        batcher.Flush(renderer);
        Logger::Error("Unknown renderable type");
        break;
    }
  }
  batcher.Flush(renderer);
  if (activeClipId != 0) {
    SDL_SetRenderClipRect(renderer, nullptr);
  }
}
