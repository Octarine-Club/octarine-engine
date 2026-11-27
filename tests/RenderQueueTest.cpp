// Unit checks for RenderKey::ComputeSortKey packing and RenderQueue's radix sort: field
// precedence (layer > depth band > type > blend > batch hash), tie stability, and the
// clustering invariants the renderer's draw batching relies on. gtest-free; exit code is
// the number of failed checks. Registered with ctest as RenderQueueTest.

#include <SDL3/SDL.h>

#include <cstdint>
#include <limits>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

#include "AssetManager/AssetManager.h"
#include "Components/CameraComponents.h"
#include "Components/CapsulePrimitiveComponent.h"
#include "Components/CirclePrimitiveComponent.h"
#include "Components/ColorGridComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/LinePrimitiveComponent.h"
#include "Components/SpriteComponent.h"
#include "Components/SquarePrimitiveComponent.h"
#include "Components/TextLabelComponent.h"
#include "Components/TrianglePrimitiveComponent.h"
#include "Components/UIRectComponent.h"
#include "ECS/Registry.h"
#include "Engine/GameModuleHooks.h"
#include "Game/EngineContext.h"
#include "Game/GameConfig.h"
#include "General/BlendMode.h"
#include "General/Constants.h"
#include "Renderer/RenderCulling.h"
#include "Renderer/RenderKey.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/RenderQueueStats.h"
#include "Renderer/Renderer.h"
#include "Renderer/SpriteRenderCache.h"
#include "Systems/RenderCapsulePrimitiveSystem.h"
#include "Systems/RenderCirclePrimitiveSystem.h"
#include "Systems/RenderColorGridSystem.h"
#include "Systems/RenderLinePrimitiveSystem.h"
#include "Systems/RenderPrimitiveSystem.h"
#include "Systems/RenderSpriteSystem.h"
#include "Systems/RenderTextSystem.h"
#include "Systems/RenderTrianglePrimitiveSystem.h"
#include "Systems/RenderUIPrimitiveSystem.h"
#include "Systems/RenderUISpriteSystem.h"
#include "TestHarness.h"

using octarine::BlendMode;
using octarine::test::Check;
using octarine::test::CheckEq;

namespace {

std::uint64_t Key(const unsigned int layer, const float depth, const RenderableType type, const void* batchKey,
                  const BlendMode blend = BlendMode::Blend) {
  return RenderKey::ComputeSortKey(layer, depth, type, batchKey, static_cast<std::uint8_t>(blend));
}

// Fake texture handles — ComputeSortKey only hashes the pointer value. 64-byte alignment keeps
// the two values apart in the bits the hash actually reads (it shifts off the low 4).
alignas(64) char g_texMemA[64];
alignas(64) char g_texMemB[64];

void TestUIPrimitiveRendering() {
  std::cout << "[ui_primitive] RenderUIPrimitiveSystem emplaces layout-driven primitives\n";
  Registry registry;
  registry.Set<RenderQueue>(RenderQueue(32));
  registry.RegisterBulkSystem(RenderUIPrimitiveSystem());

  const UIRectComponent rect(10.0f, 20.0f, 110.0f, 60.0f, 3);  // 100x40, center=(60, 40)
  registry.CreateEntityWithBundle(rect, SquarePrimitiveComponent{});
  registry.CreateEntityWithBundle(rect, CirclePrimitiveComponent{});
  registry.CreateEntityWithBundle(rect, CapsulePrimitiveComponent{});
  registry.CreateEntityWithBundle(rect, TrianglePrimitiveComponent{});
  registry.CreateEntityWithBundle(rect, LinePrimitiveComponent{});

  registry.Update(0.016f);

  const auto& queue = registry.Get<RenderQueue>();
  CheckEq(queue.Size(), static_cast<size_t>(5), "all 5 UI primitives emplaced");

  auto it = queue.begin();
  CheckEq(it->payload.square.destRect.x, 10.0f, "UI square left");
  CheckEq(it->payload.square.destRect.w, 100.0f, "UI square width");
  ++it;
  CheckEq(it->payload.circle.center.x, 60.0f, "UI circle center.x auto-fits rect");
  CheckEq(it->payload.circle.center.y, 40.0f, "UI circle center.y auto-fits rect");
  CheckEq(it->payload.circle.radiusX, 50.0f, "UI circle radiusX auto-fits rect half-width");
  CheckEq(it->payload.circle.radiusY, 20.0f, "UI circle radiusY auto-fits rect half-height");
  ++it;
  CheckEq(it->payload.capsule.radius, 20.0f, "UI capsule radius auto-fits half of minor dimension");
  CheckEq(it->payload.capsule.start.x, 30.0f, "UI capsule horizontal pill start.x = left + r");
  CheckEq(it->payload.capsule.end.x, 90.0f, "UI capsule horizontal pill end.x = right - r");
  CheckEq(it->payload.capsule.start.y, 40.0f, "UI capsule start.y = center.y");
  ++it;
  CheckEq(it->payload.triangle.p1.x, 60.0f, "UI triangle apex p1.x = midX");
  CheckEq(it->payload.triangle.p1.y, 20.0f, "UI triangle apex p1.y = top");
  CheckEq(it->payload.triangle.p2.x, 10.0f, "UI triangle p2.x = left");
  CheckEq(it->payload.triangle.p3.x, 110.0f, "UI triangle p3.x = right");
  ++it;
  CheckEq(it->payload.line.start.x, 10.0f, "UI line horizontal divider start.x = left");
  CheckEq(it->payload.line.end.x, 110.0f, "UI line horizontal divider end.x = right");
  CheckEq(it->payload.line.start.y, 40.0f, "UI line horizontal divider y = center.y");
}

void TestSoftwareRendererGeometryBatching() {
  std::cout << "[renderer_batch] GeometryBatcher & DrawStateCache via software SDL_Renderer\n";
  SDL_Surface* surface = SDL_CreateSurface(320, 240, SDL_PIXELFORMAT_RGBA32);
  SDL_Renderer* sdlRenderer = surface != nullptr ? SDL_CreateSoftwareRenderer(surface) : nullptr;
  Check(sdlRenderer != nullptr, "software SDL_Renderer created for GeometryBatcher test");
  if (sdlRenderer == nullptr) {
    SDL_DestroySurface(surface);
    return;
  }

  Renderer renderer;
  Check(renderer.CreateScene(sdlRenderer, 320, 240), "Renderer::CreateScene succeeds for batch test");

  // Create enough textures to exercise DrawStateCache hit, mutation, and clock eviction (> 8).
  constexpr size_t kTexCount = 10;
  std::vector<SDL_Texture*> textures;
  textures.reserve(kTexCount);
  for (size_t i = 0; i < kTexCount; ++i) {
    SDL_Texture* tex = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 16, 16);
    Check(tex != nullptr, "software SDL_Texture created");
    if (tex != nullptr) {
      textures.push_back(tex);
    }
  }

  if (textures.size() == kTexCount) {
    RenderQueue rq(128);
    const std::uint8_t clipId = rq.RegisterClipRect(SDL_Rect{0, 0, 320, 240});

    // Unrotated + rotated/flipped/tinted sprites on tex[0] (same batch), then switch texture & blend.
    {
      auto& s1 = rq.EmplaceSprite(0, 1.0f, textures[0], octarine::BlendMode::Blend);
      s1.texture = textures[0];
      s1.destX = 10.0f;
      s1.destY = 10.0f;
      s1.destW = 16.0f;
      s1.destH = 16.0f;
      s1.colorMod = {255, 255, 255, 255};
      s1.blendMode = SDL_BLENDMODE_BLEND;

      auto& s2 = rq.EmplaceSprite(0, 2.0f, textures[0], octarine::BlendMode::Blend);
      s2.texture = textures[0];
      s2.destX = 30.0f;
      s2.destY = 10.0f;
      s2.destW = 16.0f;
      s2.destH = 16.0f;
      s2.srcRect = {2.0f, 2.0f, 8.0f, 8.0f};
      s2.rotation = 0.5f;
      s2.pivot = {8.0f, 8.0f};
      s2.flip = static_cast<SDL_FlipMode>(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL);
      s2.colorMod = {200, 150, 100, 180};
      s2.blendMode = SDL_BLENDMODE_BLEND;
    }

    // Exercise DrawStateCache eviction + color/alpha/blend updates across 10 textures.
    for (size_t i = 0; i < kTexCount; ++i) {
      auto& s = rq.EmplaceSprite(0, 3.0f + static_cast<float>(i), textures[i], octarine::BlendMode::Blend);
      s.texture = textures[i];
      s.destX = static_cast<float>(i * 20);
      s.destY = 40.0f;
      s.destW = 12.0f;
      s.destH = 12.0f;
      s.colorMod = {255, 255, 255, 255};
      s.blendMode = (i % 2 == 0) ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_ADD;
    }

    // Squares: unrotated + rotated
    {
      auto& sq1 = rq.EmplaceSquare(1, 0.0f);
      sq1.destRect = {10.0f, 70.0f, 20.0f, 20.0f};
      sq1.color = {255, 0, 0, 255};
      sq1.blendMode = SDL_BLENDMODE_BLEND;

      auto& sq2 = rq.EmplaceSquare(1, 1.0f);
      sq2.destRect = {40.0f, 70.0f, 20.0f, 20.0f};
      sq2.rotation = 0.25f;
      sq2.pivot = {10.0f, 10.0f};
      sq2.color = {0, 255, 0, 200};
      sq2.blendMode = SDL_BLENDMODE_BLEND;
    }

    // Lines: hairline (thickness 1.0) and thick (thickness 4.0)
    {
      auto& l1 = rq.EmplaceLine(1, 2.0f);
      l1.start = {10.0f, 100.0f};
      l1.end = {60.0f, 100.0f};
      l1.thickness = 1.0f;
      l1.color = {255, 255, 255, 255};
      l1.blendMode = SDL_BLENDMODE_BLEND;

      auto& l2 = rq.EmplaceLine(1, 3.0f);
      l2.start = {10.0f, 110.0f};
      l2.end = {60.0f, 120.0f};
      l2.thickness = 4.0f;
      l2.color = {100, 200, 255, 255};
      l2.blendMode = SDL_BLENDMODE_BLEND;
    }

    // Circles: filled ellipse rotated, outline hairline, outline thick
    {
      auto& c1 = rq.EmplaceCircle(1, 4.0f);
      c1.center = {100.0f, 100.0f};
      c1.radiusX = 16.0f;
      c1.radiusY = 10.0f;
      c1.rotation = 0.3f;
      c1.filled = true;
      c1.color = {255, 200, 0, 255};
      c1.blendMode = SDL_BLENDMODE_BLEND;

      auto& c2 = rq.EmplaceCircle(1, 5.0f);
      c2.center = {140.0f, 100.0f};
      c2.radiusX = 14.0f;
      c2.radiusY = 14.0f;
      c2.filled = false;
      c2.thickness = 1.0f;
      c2.color = {0, 255, 255, 255};
      c2.blendMode = SDL_BLENDMODE_BLEND;

      auto& c3 = rq.EmplaceCircle(1, 6.0f);
      c3.center = {180.0f, 100.0f};
      c3.radiusX = 14.0f;
      c3.radiusY = 9.0f;
      c3.rotation = 0.4f;
      c3.filled = false;
      c3.thickness = 4.0f;
      c3.color = {255, 0, 255, 255};
      c3.blendMode = SDL_BLENDMODE_BLEND;
    }

    // Triangles: filled, outline hairline, outline thick
    {
      auto& t1 = rq.EmplaceTriangle(1, 7.0f);
      t1.p1 = {20.0f, 140.0f};
      t1.p2 = {10.0f, 170.0f};
      t1.p3 = {30.0f, 170.0f};
      t1.filled = true;
      t1.color = {255, 100, 100, 255};
      t1.blendMode = SDL_BLENDMODE_BLEND;

      auto& t2 = rq.EmplaceTriangle(1, 8.0f);
      t2.p1 = {50.0f, 140.0f};
      t2.p2 = {40.0f, 170.0f};
      t2.p3 = {60.0f, 170.0f};
      t2.filled = false;
      t2.thickness = 1.0f;
      t2.color = {100, 255, 100, 255};
      t2.blendMode = SDL_BLENDMODE_BLEND;

      auto& t3 = rq.EmplaceTriangle(1, 9.0f);
      t3.p1 = {80.0f, 140.0f};
      t3.p2 = {70.0f, 170.0f};
      t3.p3 = {90.0f, 170.0f};
      t3.filled = false;
      t3.thickness = 4.0f;
      t3.color = {100, 100, 255, 255};
      t3.blendMode = SDL_BLENDMODE_BLEND;
    }

    // Capsules: zero-radius, zero-length, filled, outline hairline, outline thick
    {
      auto& cap0 = rq.EmplaceCapsule(1, 10.0f);
      cap0.start = {110.0f, 150.0f};
      cap0.end = {130.0f, 150.0f};
      cap0.radius = 0.0f;
      cap0.thickness = 3.0f;
      cap0.color = {255, 255, 255, 255};
      cap0.blendMode = SDL_BLENDMODE_BLEND;

      auto& cap1 = rq.EmplaceCapsule(1, 11.0f);
      cap1.start = {145.0f, 150.0f};
      cap1.end = {145.0f, 150.0f};
      cap1.radius = 8.0f;
      cap1.filled = true;
      cap1.color = {255, 255, 0, 255};
      cap1.blendMode = SDL_BLENDMODE_BLEND;

      auto& cap2 = rq.EmplaceCapsule(1, 12.0f);
      cap2.start = {170.0f, 150.0f};
      cap2.end = {200.0f, 150.0f};
      cap2.radius = 8.0f;
      cap2.filled = true;
      cap2.color = {0, 255, 128, 255};
      cap2.blendMode = SDL_BLENDMODE_BLEND;

      auto& cap3 = rq.EmplaceCapsule(1, 13.0f);
      cap3.start = {220.0f, 150.0f};
      cap3.end = {250.0f, 150.0f};
      cap3.radius = 8.0f;
      cap3.filled = false;
      cap3.thickness = 1.0f;
      cap3.color = {255, 128, 0, 255};
      cap3.blendMode = SDL_BLENDMODE_BLEND;

      auto& cap4 = rq.EmplaceCapsule(1, 14.0f);
      cap4.start = {270.0f, 150.0f};
      cap4.end = {300.0f, 150.0f};
      cap4.radius = 8.0f;
      cap4.filled = false;
      cap4.thickness = 4.0f;
      cap4.color = {128, 128, 255, 255};
      cap4.blendMode = SDL_BLENDMODE_BLEND;
    }

    // Text and TileChunk (with and without texture) under a clip rect
    {
      auto& txt = rq.EmplaceText(2, 0.0f, textures[0], clipId);
      txt.texture = textures[0];
      txt.destRect = {10.0f, 190.0f, 40.0f, 16.0f};
      txt.colorMod = {255, 255, 255, 255};

      static constexpr SDL_Vertex kChunkVerts[3] = {
          {{60.0f, 190.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
          {{80.0f, 190.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
          {{70.0f, 210.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.5f, 1.0f}},
      };
      static constexpr int kChunkIndices[3] = {0, 1, 2};
      auto& tc1 = rq.EmplaceTileChunk(2, 1.0f, textures[0], octarine::BlendMode::Blend);
      tc1.texture = textures[0];
      tc1.vertices = kChunkVerts;
      tc1.vertexCount = 3;
      tc1.indices = kChunkIndices;
      tc1.indexCount = 3;
      tc1.blendMode = SDL_BLENDMODE_BLEND;

      auto& tc2 = rq.EmplaceTileChunk(2, 2.0f, nullptr, octarine::BlendMode::Blend);
      tc2.texture = nullptr;
      tc2.vertices = kChunkVerts;
      tc2.vertexCount = 3;
      tc2.indices = kChunkIndices;
      tc2.indexCount = 3;
      tc2.blendMode = SDL_BLENDMODE_BLEND;
    }

    // Also mutate tex[0] color/alpha/blend on a second pass to hit DrawStateCache delta branches.
    {
      auto& s = rq.EmplaceSprite(2, 3.0f, textures[0], octarine::BlendMode::Blend);
      s.texture = textures[0];
      s.destX = 100.0f;
      s.destY = 190.0f;
      s.destW = 16.0f;
      s.destH = 16.0f;
      s.colorMod = {10, 20, 30, 40};
      s.blendMode = SDL_BLENDMODE_MUL;
    }

    rq.Sort();
    renderer.BeginScene(sdlRenderer);
    renderer.DrawQueue(rq, sdlRenderer);
    renderer.EndScene(sdlRenderer, true);
    renderer.EndScene(sdlRenderer, false);
    Check(true, "GeometryBatcher & DrawStateCache DrawQueue completed without error");
  }

  for (SDL_Texture* tex : textures) {
    SDL_DestroyTexture(tex);
  }
  renderer.DestroyScene();
  SDL_DestroyRenderer(sdlRenderer);
  SDL_DestroySurface(surface);
}

void TestRenderSpriteSystem() {
  std::cout << "[sprite_system] RenderSpriteSystem snaps fixed unrotated sprites and scales world sprites\n";
  Registry registry;
  registry.Set<GameConfig>(GameConfig{});
  registry.Get<GameConfig>().windowWidth = 640;
  registry.Get<GameConfig>().windowHeight = 360;
  registry.Set<CameraComponent>(CameraComponent{});
  registry.Get<CameraComponent>().viewport = octarine::Rect{0.0f, 0.0f, 640.0f, 360.0f};
  registry.Get<CameraComponent>().zoom = 2.0f;
  registry.Set<AssetManager>(AssetManager{});
  registry.Set<SpriteRenderCache>(SpriteRenderCache{});
  registry.Set<RenderQueue>(RenderQueue(32));
  registry.RegisterSystem<GlobalTransformComponent, SpriteComponent>(RenderSpriteSystem());

  SpriteComponent fixedSprite;
  fixedSprite.isFixed = true;
  fixedSprite.width = 16.4f;
  fixedSprite.height = 16.6f;
  GlobalTransformComponent fixedXf;
  fixedXf.position = {10.4f, 20.6f};
  fixedXf.rotation = 0.0f;
  registry.CreateEntityWithBundle(fixedXf, fixedSprite);

  SpriteComponent worldSprite;
  worldSprite.isFixed = false;
  worldSprite.width = 10.0f;
  worldSprite.height = 12.0f;
  GlobalTransformComponent worldXf;
  worldXf.position = {50.0f, 60.0f};
  worldXf.rotation = 0.5f;
  registry.CreateEntityWithBundle(worldXf, worldSprite);

  registry.Update(0.016f);

  const auto& rq = registry.Get<RenderQueue>();
  CheckEq(rq.Size(), static_cast<size_t>(2), "RenderSpriteSystem emplaced 2 sprites");
  auto it = rq.begin();
  CheckEq(it->payload.sprite.destX, 10.0f, "fixed unrotated sprite x snapped");
  CheckEq(it->payload.sprite.destY, 21.0f, "fixed unrotated sprite y snapped");
  CheckEq(it->payload.sprite.destW, 16.0f, "fixed unrotated sprite width snapped");
  CheckEq(it->payload.sprite.destH, 17.0f, "fixed unrotated sprite height snapped");
  ++it;
  CheckEq(it->payload.sprite.destW, 20.0f, "world sprite width scaled by zoom");
  CheckEq(it->payload.sprite.destH, 24.0f, "world sprite height scaled by zoom");
}

void TestClaimSlotOverflowAndThreadSafeClipRects() {
  std::cout << "[concurrency] ClaimSlot overflow discardSlot & synchronized RegisterClipRect\n";
  RenderQueue rq(8);
  Check(!rq.HasOverflowed(), "queue starts without overflow");

  std::vector<std::thread> workers;
  workers.reserve(4);
  for (int t = 0; t < 4; ++t) {
    workers.emplace_back([&rq, t]() {
      for (int i = 0; i < 32; ++i) {
        const std::uint8_t cid = rq.RegisterClipRect(SDL_Rect{i % 6, t, 32, 32});
        auto& sp = rq.EmplaceSprite(0, static_cast<float>(i), nullptr, BlendMode::Blend, cid);
        sp.destX = static_cast<float>(t * 100 + i);
      }
    });
  }
  for (auto& w : workers) {
    w.join();
  }

  Check(rq.HasOverflowed(), "parallel overflow marks HasOverflowed()");
  CheckEq(rq.Size(), static_cast<size_t>(8), "Size clamps cleanly to capacity on overflow");
  rq.Sort();
  CheckEq(rq.Size(), static_cast<size_t>(8), "Sort succeeds after parallel overflow");
  Check(rq.GetClipRect(1) != nullptr, "concurrently registered clip rect is retrievable");

  rq.Clear();
  Check(!rq.HasOverflowed(), "Clear resets overflow warning flag");
  Check(rq.GetClipRect(1) == nullptr, "Clear resets registered clip rects");
}

void TestPerLayerYSortConfiguration() {
  std::cout << "[ysort] per-layer SetLayerYSort / set_layer_y_sort coalesces multi-texture layers\n";
  RenderQueue rq(256);
  Renderer::set_layer_y_sort(rq, 2, false);
  rq.SetLayerYSort(3, true, 64.0f);

  auto* tex1 = reinterpret_cast<SDL_Texture*>(g_texMemA);
  auto* tex2 = reinterpret_cast<SDL_Texture*>(g_texMemB);
  for (int i = 0; i < 32; ++i) {
    SDL_Texture* t = (i % 2 == 0) ? tex1 : tex2;
    auto& flatCmd = rq.EmplaceSprite(2, static_cast<float>(i * 53 - 400), t);
    flatCmd.texture = t;
  }
  rq.Sort();
  const auto flatStats = AnalyzeRenderQueue(rq);
  CheckEq(flatStats.drawBatches, static_cast<size_t>(2),
          "disabled Y-sort on layer 2 coalesces 32 scattered multi-texture sprites into 2 batches");

  rq.Clear();
  CheckEq(rq.EmplaceSprite(3, 5.0f, tex1).destX, 0.0f, "layer 3 emplace ok");
  rq.Clear();
  const auto kBandA =
      RenderKey::ComputeSortKey(3, 5.0f, SPRITE, tex1, 0, 0, rq.GetLayerYSort(3).bandPx, rq.GetLayerYSort(3).enabled);
  const auto kBandB =
      RenderKey::ComputeSortKey(3, 60.0f, SPRITE, tex1, 0, 0, rq.GetLayerYSort(3).bandPx, rq.GetLayerYSort(3).enabled);
  CheckEq(kBandA, kBandB, "custom 64px Y-sort band groups depth 5 and 60 together");
}

void TestSpriteAndTextCaches() {
  std::cout << "[caches] SpriteRenderCache flat vector, assetId hash validation, SweepDead, and TextRenderCache\n";
  Registry registry;
  registry.Set<GameConfig>(GameConfig{});
  registry.Get<GameConfig>().windowWidth = 640;
  registry.Get<GameConfig>().windowHeight = 360;
  registry.Set<CameraComponent>(CameraComponent{octarine::Rect{0.0f, 0.0f, 640.0f, 360.0f}, 1.0f});
  auto& am = registry.Set<AssetManager>(AssetManager{});
  auto& cache = registry.Set<SpriteRenderCache>(SpriteRenderCache{});
  registry.Set<RenderQueue>(RenderQueue(32));

  SDL_Surface* surf = SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA32);
  SDL_Renderer* sdlRenderer = surf != nullptr ? SDL_CreateSoftwareRenderer(surf) : nullptr;
  Check(sdlRenderer != nullptr, "software renderer for cache test");
  if (sdlRenderer != nullptr) {
    SDL_Surface* sA = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA32);
    SDL_Surface* sB = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA32);
    am.GetCatalog().InsertOrReplace(CatalogEntry{AssetType::Texture, "tex_a", ""});
    am.GetCatalog().InsertOrReplace(CatalogEntry{AssetType::Texture, "tex_b", ""});
    SDL_Texture* rawA = SDL_CreateTextureFromSurface(sdlRenderer, sA);
    SDL_Texture* rawB = SDL_CreateTextureFromSurface(sdlRenderer, sB);
    SDL_DestroySurface(sA);
    SDL_DestroySurface(sB);

    const Entity e1 = registry.CreateEntity();
    const Entity e2 = registry.CreateEntity();
    cache.Store(e1, rawA, SDL_FRect{0, 0, 0, 0}, 1, "tex_a");
    cache.Store(e2, rawB, SDL_FRect{0, 0, 0, 0}, 1, "tex_b");
    Check(cache.Lookup(e1, 1, "tex_a") != nullptr, "SpriteRenderCache hit on matching assetId");
    Check(cache.Lookup(e1, 1, "tex_b") == nullptr, "SpriteRenderCache invalidates on C++ assetId mutation");

    registry.BlamEntity(e2);
    cache.SweepDead([&registry](const Entity e) { return registry.IsAlive(e); });
    CheckEq(cache.ActiveEntryCount(), static_cast<size_t>(1), "SweepDead evicts destroyed entity slot");
    registry.BlamEntity(e1);
    cache.SweepDead([&registry](const Entity e) { return registry.IsAlive(e); });
    CheckEq(cache.SlotCapacity(), static_cast<size_t>(0), "SweepDead trims trailing empty slots");

    EngineContext ectx{};
    ectx.sdlRenderer = sdlRenderer;
    registry.Set<EngineContext>(ectx);
    RenderTextSystem rts;
    rts.Prepare(&registry);
    Check(registry.TryGet<TextRenderCache>() != nullptr,
          "RenderTextSystem::Prepare installs TextRenderCache singleton");
    RenderTextSystem::ClearRegistryCache(registry);
    CheckEq(rts.ContentCacheSize(), static_cast<size_t>(0), "ClearRegistryCache clears TextRenderCache");

    SDL_DestroyTexture(rawA);
    SDL_DestroyTexture(rawB);
    SDL_DestroyRenderer(sdlRenderer);
  }
  SDL_DestroySurface(surf);
}

void TestWorldPrimitiveDepthAndRotatedNegativeScaleCulling() {
  std::cout << "[cull+depth] World maxY depth in Line/Triangle/Capsule and rotated/negative-scale AABB culling\n";
  Registry registry;
  registry.Set<GameConfig>(GameConfig{});
  registry.Get<GameConfig>().windowWidth = 200;
  registry.Get<GameConfig>().windowHeight = 200;
  registry.Set<CameraComponent>(CameraComponent{octarine::Rect{0.0f, 0.0f, 200.0f, 200.0f}, 1.0f});
  registry.Set<AssetManager>(AssetManager{});
  registry.Set<SpriteRenderCache>(SpriteRenderCache{});
  registry.Set<RenderQueue>(RenderQueue(32));

  RenderLinePrimitiveSystem lineSys;
  RenderTrianglePrimitiveSystem triSys;
  RenderCapsulePrimitiveSystem capSys;
  lineSys.Prepare(&registry);
  triSys.Prepare(&registry);
  capSys.Prepare(&registry);

  GlobalTransformComponent nearXf;
  nearXf.position = {20.0f, 10.0f};
  GlobalTransformComponent farXf;
  farXf.position = {20.0f, 150.0f};

  LinePrimitiveComponent line({0.0f, 0.0f}, {10.0f, 5.0f}, 0, 2.0f);
  line.isFixed = false;
  TrianglePrimitiveComponent tri({0.0f, 0.0f}, {10.0f, 0.0f}, {5.0f, 8.0f}, 0);
  tri.isFixed = false;
  CapsulePrimitiveComponent cap({0.0f, 0.0f}, {0.0f, 12.0f}, 0, 4.0f);
  cap.isFixed = false;

  // Emplace far world entity first (with local start.y == 0), then near world entity (with local start.y == 5).
  // Sorting must place nearXf BEFORE farXf using world maxY rather than local start.y!
  lineSys(line, farXf);
  LinePrimitiveComponent nearLine({0.0f, 5.0f}, {10.0f, 8.0f}, 0, 2.0f);
  nearLine.isFixed = false;
  lineSys(nearLine, nearXf);

  auto& rq = registry.Get<RenderQueue>();
  rq.Sort();
  Check(rq.begin()->payload.line.start.y < (rq.begin() + 1)->payload.line.start.y,
        "RenderLinePrimitiveSystem sorts by world maxY instead of local start.y");

  rq.Clear();
  triSys(tri, farXf);
  TrianglePrimitiveComponent nearTri({0.0f, 4.0f}, {10.0f, 4.0f}, {5.0f, 9.0f}, 0);
  nearTri.isFixed = false;
  triSys(nearTri, nearXf);
  rq.Sort();
  Check(rq.begin()->payload.triangle.p1.y < (rq.begin() + 1)->payload.triangle.p1.y,
        "RenderTrianglePrimitiveSystem sorts by world maxY instead of local p1.y");

  rq.Clear();
  capSys(cap, farXf);
  CapsulePrimitiveComponent nearCap({0.0f, 6.0f}, {0.0f, 10.0f}, 0, 4.0f);
  nearCap.isFixed = false;
  capSys(nearCap, nearXf);
  rq.Sort();
  Check(rq.begin()->payload.capsule.start.y < (rq.begin() + 1)->payload.capsule.start.y,
        "RenderCapsulePrimitiveSystem sorts by world maxY instead of local start.y");

  // Negative-scale & rotated culling check:
  // Origin at x = -10 (outside camera [0..200]), width = -40 -> normalized [-50..-10] is outside;
  // Origin at x = 20, scale.x = -2, width = 30 -> w = -60 -> normalized [-40..20] overlaps [0..200]!
  const octarine::Rect cam{0.0f, 0.0f, 200.0f, 200.0f};
  Check(!IsRenderableOutsideViewport(20.0f, 50.0f, -60.0f, 20.0f, false, cam, 200.0f, 200.0f),
        "negative-width mirrored renderable overlapping camera left edge is NOT falsely culled");
  // Unrotated [210..310] x [50..60] is outside camera [0..200], but rotated +pi around pivot (0,0) lands in [110..210]!
  Check(!IsRotatedQuadOutsideViewport(210.0f, 50.0f, 100.0f, 10.0f, 0.0f, 0.0f, std::numbers::pi_v<float>, false, cam,
                                      200.0f, 200.0f),
        "rotated quad swinging into viewport around offset pivot is NOT falsely culled");
}

void TestGradientsBatchedTextAndPostScenePass() {
  std::cout << "[gradients+passes] 4-corner Square/Sprite gradients, batched TEXT, and Renderer post-scene hook\n";
  SDL_Surface* surf = SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA32);
  SDL_Renderer* sdlRenderer = surf != nullptr ? SDL_CreateSoftwareRenderer(surf) : nullptr;
  Check(sdlRenderer != nullptr, "software renderer for gradient & post-pass test");
  if (sdlRenderer == nullptr) {
    SDL_DestroySurface(surf);
    return;
  }

  Renderer renderer;
  Check(renderer.CreateScene(sdlRenderer, 64, 64), "CreateScene 64x64");

  Registry reg;
  static int hookInvocations = 0;
  hookInvocations = 0;
  game_module::SetRenderPassHook([](Registry&, Renderer&) { ++hookInvocations; });
  game_module::InvokeRegisterRenderPasses(reg, renderer);
  game_module::SetRenderPassHook(nullptr);
  CheckEq(hookInvocations, 1, "SetRenderPassHook registers portable post-scene pass callback");

  int passInvocations = 0;
  renderer.AddPostScenePass([&passInvocations](const Renderer::RenderPassContext& ctx) {
    if (ctx.sdlRenderer != nullptr && ctx.sceneTexture != nullptr && ctx.logicalWidth == 64) {
      ++passInvocations;
    }
  });
  CheckEq(renderer.PostScenePassCount(), static_cast<size_t>(1), "AddPostScenePass registers 1 callback");

  SDL_Surface* whiteSurf = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA32);
  SDL_FillSurfaceRect(whiteSurf, nullptr, SDL_MapSurfaceRGBA(whiteSurf, 255, 255, 255, 255));
  SDL_Texture* whiteTex = SDL_CreateTextureFromSurface(sdlRenderer, whiteSurf);
  SDL_DestroySurface(whiteSurf);

  RenderQueue rq(64);
  auto& gradSq = rq.EmplaceSquare(0, 0.0f, nullptr, BlendMode::None);
  gradSq.destRect = {0.0f, 0.0f, 32.0f, 32.0f};
  gradSq.blendMode = SDL_BLENDMODE_NONE;
  gradSq.SetVerticalGradient(SDL_Color{255, 0, 0, 255}, SDL_Color{0, 0, 255, 255});

  auto& gradSp = rq.EmplaceSpriteGradient(
      0, 0.0f, QuadGradient::Horizontal(SDL_Color{0, 255, 0, 255}, SDL_Color{255, 255, 0, 255}), whiteTex,
      BlendMode::None, 0, 1);
  gradSp.texture = whiteTex;
  gradSp.destX = 32.0f;
  gradSp.destY = 0.0f;
  gradSp.destW = 32.0f;
  gradSp.destH = 32.0f;
  gradSp.blendMode = SDL_BLENDMODE_NONE;

  // Emit 9 outlined TEXT commands sharing whiteTex to exercise GeometryBatcher::PushText coalescing.
  for (int i = 0; i < 9; ++i) {
    auto& tc = rq.EmplaceText(1, 0.0f, whiteTex);
    tc.texture = whiteTex;
    tc.destRect = {static_cast<float>(4 + i), 40.0f, 8.0f, 8.0f};
    tc.colorMod = (i == 8) ? SDL_Color{255, 255, 255, 255} : SDL_Color{20, 40, 60, 255};
  }

  rq.Sort();
  renderer.BeginScene(sdlRenderer);
  renderer.DrawQueue(rq, sdlRenderer);
  renderer.EndScene(sdlRenderer, false);

  CheckEq(passInvocations, 1, "Post-scene render pass invoked during DrawQueue");

  SDL_SetRenderTarget(sdlRenderer, renderer.GetSceneTexture());
  SDL_Surface* readback = SDL_RenderReadPixels(sdlRenderer, nullptr);
  SDL_SetRenderTarget(sdlRenderer, nullptr);
  Check(readback != nullptr, "readback scene target for gradient verification");
  if (readback != nullptr) {
    Uint8 rTop = 0, gTop = 0, bTop = 0, aTop = 0;
    Uint8 rBot = 0, gBot = 0, bBot = 0, aBot = 0;
    SDL_ReadSurfacePixel(readback, 8, 2, &rTop, &gTop, &bTop, &aTop);
    SDL_ReadSurfacePixel(readback, 8, 29, &rBot, &gBot, &bBot, &aBot);
    Check(rTop > 200 && bTop < 60, "Square vertical gradient top is red-dominant");
    Check(bBot > 200 && rBot < 60, "Square vertical gradient bottom is blue-dominant");

    Uint8 rLeft = 0, gLeft = 0, bLeft = 0, aLeft = 0;
    Uint8 rRight = 0, gRight = 0, bRight = 0, aRight = 0;
    SDL_ReadSurfacePixel(readback, 34, 16, &rLeft, &gLeft, &bLeft, &aLeft);
    SDL_ReadSurfacePixel(readback, 61, 16, &rRight, &gRight, &bRight, &aRight);
    Check(gLeft > 200 && rLeft < 60, "Sprite horizontal gradient left is green");
    Check(rRight > 200 && gRight > 200 && bRight < 60, "Sprite horizontal gradient right is yellow");

    Uint8 rTxt = 0, gTxt = 0, bTxt = 0, aTxt = 0;
    SDL_ReadSurfacePixel(readback, 14, 44, &rTxt, &gTxt, &bTxt, &aTxt);
    Check(rTxt > 220 && gTxt > 220 && bTxt > 220, "Batched TEXT foreground glyph rendered white");
    SDL_DestroySurface(readback);
  }

  renderer.ClearPostScenePasses();
  CheckEq(renderer.PostScenePassCount(), static_cast<size_t>(0), "ClearPostScenePasses clears callbacks");
  SDL_DestroyTexture(whiteTex);
  renderer.DestroyScene();
  SDL_DestroyRenderer(sdlRenderer);
  SDL_DestroySurface(surf);
}

void TestExtremeDepthAndNegativeLayerClamp() {
  std::cout << "[sortkey+layers] Extreme finite depth saturates cleanly & negative layers clamp to 0\n";
  // Extreme finite depth values must saturate in the 17-bit depthBand without int64 cast overflow
  // or bleeding into layer bits.
  const std::uint64_t hiKey = RenderKey::ComputeSortKey(0u, 1e30f, SPRITE, nullptr);
  const std::uint64_t loKey = RenderKey::ComputeSortKey(0u, -1e30f, SPRITE, nullptr);
  const std::uint64_t maxTinyBand =
      RenderKey::ComputeSortKey(0u, std::numeric_limits<float>::max(), SPRITE, nullptr, 0u, 0u, 1e-35f, true);
  const std::uint64_t layer1Key = RenderKey::ComputeSortKey(1u, -1e30f, SPRITE, nullptr);
  Check(hiKey > loKey, "extreme positive finite depth sorts after extreme negative finite depth");
  CheckEq(hiKey >> 48, static_cast<std::uint64_t>(0), "extreme depth does not bleed into layer field");
  CheckEq(maxTinyBand >> 48, static_cast<std::uint64_t>(0), "overflowed depth/band quotient stays in layer 0");
  Check(layer1Key > maxTinyBand, "layer 1 still outranks saturated layer 0 depth");

  // Negative layers in Render* systems clamp to layer 0 rather than wrapping to 65531 above layer 1.
  Registry reg;
  auto& cfg = reg.Set<GameConfig>(GameConfig{});
  cfg.windowWidth = 640;
  cfg.windowHeight = 480;
  auto& cam = reg.Set<CameraComponent>(CameraComponent{});
  cam.viewport = octarine::Rect{0.0f, 0.0f, 640.0f, 480.0f};
  cam.zoom = 1.0f;
  reg.Set<AssetManager>(AssetManager{});
  reg.Set<SpriteRenderCache>(SpriteRenderCache{});
  auto& rq = reg.Set<RenderQueue>(RenderQueue(64));

  GlobalTransformComponent xf;
  xf.position = {32.0f, 32.0f};

  RenderSpriteSystem spriteSys;
  spriteSys.Prepare(&reg);
  SpriteComponent negSprite;
  negSprite.layer = -5;
  negSprite.width = 16.0f;
  negSprite.height = 16.0f;
  const Entity spEnt = reg.CreateEntityWithBundle(xf, negSprite);
  spriteSys(spEnt, xf, negSprite);

  RenderPrimitiveSystem sqSys;
  sqSys.Prepare(&reg);
  SquarePrimitiveComponent negSq;
  negSq.layer = -5;
  negSq.width = 16.0f;
  negSq.height = 16.0f;
  sqSys(negSq, xf);

  RenderCirclePrimitiveSystem circSys;
  circSys.Prepare(&reg);
  CirclePrimitiveComponent negCirc;
  negCirc.layer = -5;
  negCirc.radius = 8.0f;
  circSys(negCirc, xf);

  RenderLinePrimitiveSystem lineSys;
  lineSys.Prepare(&reg);
  LinePrimitiveComponent negLine;
  negLine.layer = -5;
  negLine.end = {16.0f, 0.0f};
  lineSys(negLine, xf);

  RenderTrianglePrimitiveSystem triSys;
  triSys.Prepare(&reg);
  TrianglePrimitiveComponent negTri;
  negTri.layer = -5;
  negTri.p2 = {16.0f, 0.0f};
  negTri.p3 = {0.0f, 16.0f};
  triSys(negTri, xf);

  RenderCapsulePrimitiveSystem capSys;
  capSys.Prepare(&reg);
  CapsulePrimitiveComponent negCap;
  negCap.layer = -5;
  negCap.end = {16.0f, 0.0f};
  negCap.radius = 4.0f;
  capSys(negCap, xf);

  UIRectComponent negUiRect{0.0f, 0.0f, 32.0f, 32.0f};
  negUiRect.visible = true;
  negUiRect.layer = -5;
  const Entity uiSprEnt = reg.CreateEntityWithBundle(negUiRect, negSprite);
  RenderUISpriteSystem uiSprSys;
  uiSprSys.Prepare(&reg);
  ContextFacade uiCtx(&reg, 0.0f, nullptr, nullptr);
  uiSprSys(ContextFacade(&reg, 0.0f, nullptr, nullptr), uiSprEnt, negUiRect, negSprite);
  (void)uiCtx;

  const Entity uiSqEnt = reg.CreateEntityWithBundle(negUiRect, negSq);
  (void)uiSqEnt;
  reg.RegisterBulkSystem(RenderUIPrimitiveSystem());
  reg.Update(0.016f);

  Check(!rq.IsEmpty(), "negative-layer render systems emplaced commands");
  for (const RenderKey& key : rq) {
    CheckEq(static_cast<unsigned int>(key.sortKey >> 48), 0u,
            "negative component/UI layer clamps to render layer 0 instead of wrapping to 65531");
  }
}

}  // namespace

int main() {
  const void* texA = g_texMemA;
  const void* texB = g_texMemB;

  std::cout << "[sortkey] field precedence\n";
  {
    Check(Key(1, 0.0f, SPRITE, nullptr) > Key(0, 1000000.0f, SPRITE, nullptr), "layer outranks any depth difference");
    Check(Key(0, 2.0f * RenderKey::kDefaultYBandPx, SPRITE, nullptr) > Key(0, 0.0f, SPRITE, nullptr),
          "deeper band sorts later within a layer");
    CheckEq(Key(0, 0.25f * RenderKey::kDefaultYBandPx, SPRITE, nullptr),
            Key(0, 0.75f * RenderKey::kDefaultYBandPx, SPRITE, nullptr), "depths inside one band share a key");
    Check(Key(0, -2.0f * RenderKey::kDefaultYBandPx, SPRITE, nullptr) < Key(0, 0.0f, SPRITE, nullptr),
          "negative depth sorts before zero (band bias)");
    Check(RenderKey::ComputeSortKey(0, 0.0f, SPRITE, nullptr, 0, 1) >
              RenderKey::ComputeSortKey(0, 0.0f, TILE_CHUNK, nullptr, 0, 0),
          "materialId clusters within depth band before type/blend/texture");
    Check(Key(0, 0.0f, SPRITE, nullptr) != Key(0, 0.0f, SQUARE_PRIMITIVE, nullptr), "type participates in the key");
    Check(Key(0, 0.0f, SPRITE, nullptr) != Key(0, 0.0f, CIRCLE_PRIMITIVE, nullptr), "circle type participates in key");
    Check(Key(0, 0.0f, SPRITE, nullptr) != Key(0, 0.0f, LINE_PRIMITIVE, nullptr), "line type participates in key");
    Check(Key(0, 0.0f, SPRITE, nullptr) != Key(0, 0.0f, TRIANGLE_PRIMITIVE, nullptr),
          "triangle type participates in key");
    Check(Key(0, 0.0f, SPRITE, nullptr) != Key(0, 0.0f, CAPSULE_PRIMITIVE, nullptr),
          "capsule type participates in key");
    Check(Key(0, 0.0f, SPRITE, texA) != Key(0, 0.0f, SPRITE, texB), "distinct textures get distinct batch hashes");
    CheckEq(Key(0, 0.0f, SPRITE, texA), Key(0, 0.0f, SPRITE, texA), "key is deterministic");
    // Blend sits above the texture hash inside the batch bits, so every same-blend run clusters
    // before texture clustering — regardless of which texture pointers are involved.
    Check(Key(0, 0.0f, SPRITE, texA, BlendMode::Blend) < Key(0, 0.0f, SPRITE, texB, BlendMode::Add) &&
              Key(0, 0.0f, SPRITE, texB, BlendMode::Blend) < Key(0, 0.0f, SPRITE, texA, BlendMode::Add),
          "blend mode outranks texture hash");
  }

  std::cout << "[sort] orders by key, ties keep insertion order\n";
  {
    RenderQueue queue(1024);
    // Three layers emplaced in reverse; four entries per layer marked with their insertion index.
    int idx = 0;
    for (const unsigned int layer : {2u, 1u, 0u}) {
      for (int i = 0; i < 4; ++i) {
        auto& cmd = queue.EmplaceSprite(layer, 0.0f, texA);
        cmd.destX = static_cast<float>(idx++);
      }
    }
    queue.Sort();
    CheckEq(queue.Size(), static_cast<size_t>(12), "Size unchanged by Sort");

    bool ascending = true;
    std::vector<float> markers;
    std::uint64_t prev = 0;
    for (const RenderKey& key : queue) {
      if (!markers.empty() && key.sortKey < prev) ascending = false;
      prev = key.sortKey;
      markers.push_back(key.payload.sprite.destX);
    }
    Check(ascending, "sortKey is non-decreasing after Sort");
    Check(markers[0] == 8 && markers[4] == 4 && markers[8] == 0, "layer groups come out 0, 1, 2");
    bool stable = true;
    for (int group = 0; group < 3; ++group) {
      for (int i = 1; i < 4; ++i) {
        if (markers[group * 4 + i] != markers[group * 4 + i - 1] + 1.0f) stable = false;
      }
    }
    Check(stable, "tied keys keep insertion order (stable radix passes)");
  }

  std::cout << "[sort] blend + texture clustering within one layer/band\n";
  {
    RenderQueue queue(1024);
    // Worst-case interleave of (blend, texture) combos: 16 sprites alternating blend every entry
    // and texture every other entry. After Sort the queue must contain exactly one blend boundary
    // and one texture boundary inside each blend run.
    const BlendMode blends[2] = {BlendMode::Blend, BlendMode::Add};
    const void* textures[2] = {texA, texB};
    for (int i = 0; i < 16; ++i) {
      const BlendMode blend = blends[i % 2];
      const void* texture = textures[(i / 2) % 2];
      auto& cmd = queue.EmplaceSprite(0, 0.0f, texture, blend);
      cmd.texture = static_cast<SDL_Texture*>(const_cast<void*>(texture));
      cmd.blendMode = octarine::ToSdlBlendMode(blend);
    }
    queue.Sort();

    int blendTransitions = 0;
    int textureTransitions = 0;
    SDL_BlendMode prevBlend{};
    SDL_Texture* prevTexture{};
    bool first = true;
    for (const RenderKey& key : queue) {
      const auto& cmd = key.payload.sprite;
      if (!first) {
        if (cmd.blendMode != prevBlend) {
          ++blendTransitions;
        } else if (cmd.texture != prevTexture) {
          ++textureTransitions;
        }
      }
      prevBlend = cmd.blendMode;
      prevTexture = cmd.texture;
      first = false;
    }
    CheckEq(blendTransitions, 1, "same-blend runs cluster (one blend boundary)");
    CheckEq(textureTransitions, 2, "textures cluster within each blend run");
  }

  std::cout << "[sort] type clusters within one layer/band\n";
  {
    RenderQueue queue(256);
    queue.EmplaceSquare(0, 0.0f);
    queue.EmplaceSprite(0, 0.0f, texA);
    queue.EmplaceText(0, 0.0f);
    queue.EmplaceSquare(0, 0.0f);
    queue.EmplaceSprite(0, 0.0f, texA);
    queue.Sort();

    int typeTransitions = 0;
    RenderableType prevType{};
    bool first = true;
    for (const RenderKey& key : queue) {
      if (!first && key.type != prevType) ++typeTransitions;
      prevType = key.type;
      first = false;
    }
    CheckEq(typeTransitions, 2, "three types -> two type boundaries");
  }

  std::cout << "[lifecycle] Clear resets, queue is reusable\n";
  {
    RenderQueue queue(64);
    queue.EmplaceSprite(0, 0.0f, nullptr);
    Check(!queue.IsEmpty(), "queue non-empty after emplace");
    queue.Clear();
    Check(queue.IsEmpty() && queue.Size() == 0 && queue.begin() == queue.end(), "Clear resets the queue");
    auto& cmd = queue.EmplaceSprite(0, 0.0f, nullptr);
    cmd.destX = 42.0f;
    queue.Sort();
    CheckEq(queue.Size(), static_cast<size_t>(1), "queue reusable after Clear");
    CheckEq(queue.begin()->payload.sprite.destX, 42.0f, "payload survives the post-Clear sort");
  }

  std::cout << "[primitive] RenderPrimitiveSystem applies transform scale\n";
  {
    // The system reads its singletons in Prepare(), then writes straight into the queue, so a
    // bare registry with the three singletons set is enough to exercise it.
    Registry registry;
    registry.Set<GameConfig>(GameConfig{});
    registry.Get<GameConfig>().windowWidth = 800;
    registry.Get<GameConfig>().windowHeight = 600;
    registry.Set<CameraComponent>(CameraComponent{});
    registry.Set<RenderQueue>(RenderQueue(16));

    RenderPrimitiveSystem system;
    system.Prepare(&registry);

    SquarePrimitiveComponent square(glm::vec2(10.0f, 20.0f), 0, 30.0f, 40.0f);
    GlobalTransformComponent transform;
    transform.position = {100.0f, 100.0f};
    transform.scale = {2.0f, 3.0f};

    system(square, transform);

    const auto& queue = registry.Get<RenderQueue>();
    CheckEq(queue.Size(), static_cast<size_t>(1), "square emplaced");
    const auto& cmd = queue.begin()->payload.square;
    CheckEq(cmd.destRect.w, 60.0f, "square width scales with transform.scale.x");
    CheckEq(cmd.destRect.h, 120.0f, "square height scales with transform.scale.y");
    // Local offset (10, 20) scaled by (2, 3) -> (20, 60), added to the transform position.
    CheckEq(cmd.destRect.x, 120.0f, "square local offset scales on x");
    CheckEq(cmd.destRect.y, 160.0f, "square local offset scales on y");
  }

  std::cout << "[primitive] a centre pivot lands mid-quad at any scale\n";
  {
    Registry registry;
    registry.Set<GameConfig>(GameConfig{});
    registry.Get<GameConfig>().windowWidth = 800;
    registry.Get<GameConfig>().windowHeight = 600;
    registry.Set<CameraComponent>(CameraComponent{});
    registry.Set<RenderQueue>(RenderQueue(16));

    RenderPrimitiveSystem system;
    system.Prepare(&registry);

    SquarePrimitiveComponent square(glm::vec2(0.0f, 0.0f), 0, 30.0f, 40.0f);
    GlobalTransformComponent transform;
    transform.position = {0.0f, 0.0f};
    transform.scale = {2.0f, 3.0f};
    // What TransformSystem writes for a (0.5, 0.5) pivot: normalized * size * scale.
    transform.pivot = {30.0f, 60.0f};

    system(square, transform);

    const auto& cmd = registry.Get<RenderQueue>().begin()->payload.square;
    CheckEq(cmd.pivot.x, cmd.destRect.w * 0.5f, "centre pivot stays centred on x under scale");
    CheckEq(cmd.pivot.y, cmd.destRect.h * 0.5f, "centre pivot stays centred on y under scale");
  }

  std::cout << "[primitive] RenderCirclePrimitiveSystem emplaces and scales circle\n";
  {
    Registry registry;
    registry.Set<GameConfig>(GameConfig{});
    registry.Get<GameConfig>().windowWidth = 800;
    registry.Get<GameConfig>().windowHeight = 600;
    registry.Set<CameraComponent>(CameraComponent{});
    registry.Set<RenderQueue>(RenderQueue(16));

    RenderCirclePrimitiveSystem system;
    system.Prepare(&registry);

    CirclePrimitiveComponent circle(glm::vec2(10.0f, 20.0f), 1, 15.0f);
    GlobalTransformComponent transform;
    transform.position = {100.0f, 100.0f};
    transform.scale = {2.0f, 2.0f};

    system(circle, transform);

    const auto& queue = registry.Get<RenderQueue>();
    CheckEq(queue.Size(), static_cast<size_t>(1), "circle emplaced");
    const auto& cmd = queue.begin()->payload.circle;
    CheckEq(cmd.radiusX, 30.0f, "circle radius scales with transform");
    CheckEq(cmd.radiusY, 30.0f, "circle radius scales on y");
    CheckEq(cmd.center.x, 120.0f, "circle center x accounts for scale and offset");
    CheckEq(cmd.center.y, 140.0f, "circle center y accounts for scale and offset");
    Check(cmd.filled, "circle is filled by default");
  }

  std::cout << "[primitive] RenderLinePrimitiveSystem emplaces line\n";
  {
    Registry registry;
    registry.Set<GameConfig>(GameConfig{});
    registry.Get<GameConfig>().windowWidth = 800;
    registry.Get<GameConfig>().windowHeight = 600;
    registry.Set<CameraComponent>(CameraComponent{});
    registry.Set<RenderQueue>(RenderQueue(16));

    RenderLinePrimitiveSystem system;
    system.Prepare(&registry);

    LinePrimitiveComponent line(glm::vec2(0.0f, 0.0f), glm::vec2(50.0f, 50.0f), 0, 2.0f);
    GlobalTransformComponent transform;
    transform.position = {10.0f, 20.0f};
    transform.scale = {1.0f, 1.0f};

    system(line, transform);

    const auto& queue = registry.Get<RenderQueue>();
    CheckEq(queue.Size(), static_cast<size_t>(1), "line emplaced");
    const auto& cmd = queue.begin()->payload.line;
    CheckEq(cmd.start.x, 10.0f, "line start.x matches world position");
    CheckEq(cmd.start.y, 20.0f, "line start.y matches world position");
    CheckEq(cmd.end.x, 60.0f, "line end.x matches world position");
    CheckEq(cmd.end.y, 70.0f, "line end.y matches world position");
    CheckEq(cmd.thickness, 2.0f, "line thickness preserved");
  }

  std::cout << "[primitive] RenderTrianglePrimitiveSystem emplaces triangle\n";
  {
    Registry registry;
    registry.Set<GameConfig>(GameConfig{});
    registry.Get<GameConfig>().windowWidth = 800;
    registry.Get<GameConfig>().windowHeight = 600;
    registry.Set<CameraComponent>(CameraComponent{});
    registry.Set<RenderQueue>(RenderQueue(16));

    RenderTrianglePrimitiveSystem system;
    system.Prepare(&registry);

    TrianglePrimitiveComponent triangle(glm::vec2(0.0f, 0.0f), glm::vec2(10.0f, 0.0f), glm::vec2(5.0f, 10.0f));
    GlobalTransformComponent transform;
    transform.position = {50.0f, 50.0f};
    transform.scale = {1.0f, 1.0f};

    system(triangle, transform);

    const auto& queue = registry.Get<RenderQueue>();
    CheckEq(queue.Size(), static_cast<size_t>(1), "triangle emplaced");
    const auto& cmd = queue.begin()->payload.triangle;
    CheckEq(cmd.p1.x, 50.0f, "triangle p1.x");
    CheckEq(cmd.p2.x, 60.0f, "triangle p2.x");
    CheckEq(cmd.p3.y, 60.0f, "triangle p3.y");
    Check(cmd.filled, "triangle filled by default");
  }

  std::cout << "[primitive] RenderCapsulePrimitiveSystem emplaces capsule\n";
  {
    Registry registry;
    registry.Set<GameConfig>(GameConfig{});
    registry.Get<GameConfig>().windowWidth = 800;
    registry.Get<GameConfig>().windowHeight = 600;
    registry.Set<CameraComponent>(CameraComponent{});
    registry.Set<RenderQueue>(RenderQueue(16));

    RenderCapsulePrimitiveSystem system;
    system.Prepare(&registry);

    CapsulePrimitiveComponent capsule(glm::vec2(0.0f, 0.0f), glm::vec2(0.0f, 40.0f), 0, 10.0f);
    GlobalTransformComponent transform;
    transform.position = {100.0f, 100.0f};
    transform.scale = {1.0f, 1.0f};

    system(capsule, transform);

    const auto& queue = registry.Get<RenderQueue>();
    CheckEq(queue.Size(), static_cast<size_t>(1), "capsule emplaced");
    const auto& cmd = queue.begin()->payload.capsule;
    CheckEq(cmd.start.x, 100.0f, "capsule start.x");
    CheckEq(cmd.end.y, 140.0f, "capsule end.y");
    CheckEq(cmd.radius, 10.0f, "capsule radius");
  }

  std::cout << "[color_grid] RenderColorGridSystem auto-scrolls and emits batched squares\n";
  {
    Registry registry;
    registry.Set<GameConfig>(GameConfig{});
    registry.Get<GameConfig>().windowWidth = 100;
    registry.Get<GameConfig>().windowHeight = 100;
    registry.Set<CameraComponent>(CameraComponent{});
    registry.Get<CameraComponent>().viewport = octarine::Rect{0.0f, 0.0f, 100.0f, 100.0f};
    registry.Set<RenderQueue>(RenderQueue(32));

    RenderColorGridSystem system;
    system.Prepare(&registry);

    ColorGridComponent grid(50.0f, 50.0f);
    grid.scrollVelocity = {10.0f, 20.0f};
    GlobalTransformComponent transform;
    transform.position = {0.0f, 0.0f};
    transform.scale = {1.0f, 1.0f};

    // dt = 0.5s -> offset becomes (5, 10)
    system(0.5f, grid, transform);
    CheckEq(grid.offset.x, 5.0f, "scroll velocity integrates x into offset");
    CheckEq(grid.offset.y, 10.0f, "scroll velocity integrates y into offset");

    auto& queue = registry.Get<RenderQueue>();
    Check(!queue.IsEmpty(), "color grid emitted commands into queue");

    // Opaque 100x100 viewport with 50x50 cells:
    // Emits 1 full-viewport background square (color1, batchKey 0x100)
    // plus alternating squares (color2, batchKey 0x200).
    const auto& firstCmd = queue.begin()->payload.square;
    CheckEq(firstCmd.destRect.w, 100.0f, "opaque background width covers visible viewport");
    CheckEq(firstCmd.destRect.h, 100.0f, "opaque background height covers visible viewport");

    queue.Sort();
    // Sort radix groups all same-batchKey primitives together
    std::vector<uint64_t> sortKeys;
    for (const auto& key : queue) {
      sortKeys.push_back(key.sortKey);
    }
    for (size_t i = 1; i < sortKeys.size(); ++i) {
      Check(sortKeys[i - 1] <= sortKeys[i], "render keys sorted in non-descending order");
    }
  }

  std::cout << "[color_grid] RenderColorGridSystem culls bounded grid when outside viewport\n";
  {
    Registry registry;
    registry.Set<GameConfig>(GameConfig{});
    registry.Get<GameConfig>().windowWidth = 100;
    registry.Get<GameConfig>().windowHeight = 100;
    registry.Set<CameraComponent>(CameraComponent{});
    registry.Get<CameraComponent>().viewport = octarine::Rect{0.0f, 0.0f, 100.0f, 100.0f};
    registry.Set<RenderQueue>(RenderQueue(32));

    RenderColorGridSystem system;
    system.Prepare(&registry);

    ColorGridComponent grid(50.0f, 50.0f);
    grid.bounds = {200.0f, 200.0f};
    GlobalTransformComponent transform;
    transform.position = {500.0f, 500.0f};  // Way outside (0, 0, 100, 100)

    system(0.0f, grid, transform);
    const auto& queue = registry.Get<RenderQueue>();
    Check(queue.IsEmpty(), "bounded grid completely outside camera is culled");
  }

  TestUIPrimitiveRendering();
  TestSoftwareRendererGeometryBatching();
  TestRenderSpriteSystem();
  TestClaimSlotOverflowAndThreadSafeClipRects();
  TestPerLayerYSortConfiguration();
  TestSpriteAndTextCaches();
  TestWorldPrimitiveDepthAndRotatedNegativeScaleCulling();
  TestGradientsBatchedTextAndPostScenePass();
  TestExtremeDepthAndNegativeLayerClamp();

  std::cout << "[render_queue] ClearGeneration increments on Clear and survives move\n";
  {
    RenderQueue rq(8);
    CheckEq(rq.ClearGeneration(), static_cast<std::uint64_t>(0), "ClearGeneration starts at 0");
    rq.Clear();
    CheckEq(rq.ClearGeneration(), static_cast<std::uint64_t>(1), "ClearGeneration increments on Clear");
    RenderQueue moved(std::move(rq));
    CheckEq(moved.ClearGeneration(), static_cast<std::uint64_t>(1), "ClearGeneration preserved across move ctor");
    moved.Clear();
    CheckEq(moved.ClearGeneration(), static_cast<std::uint64_t>(2), "ClearGeneration increments after move");
  }

  return octarine::test::ReportSummary("RenderQueueTest");
}
