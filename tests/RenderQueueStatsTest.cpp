#include <cstdint>

#include "General/BlendMode.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/RenderQueueStats.h"
#include "TestHarness.h"

using octarine::test::Check;
using octarine::test::CheckEq;

int main() {
  // Test 1: Empty queue
  {
    RenderQueue queue(16);
    const auto stats = AnalyzeRenderQueue(queue);
    CheckEq(stats.commandCount, size_t{0}, "Empty queue has 0 commands");
    CheckEq(stats.drawBatches, size_t{0}, "Empty queue has 0 batches");
    CheckEq(stats.totalVertices, size_t{0}, "Empty queue has 0 vertices");
    CheckEq(stats.keysPerBatch, 0.0, "Empty queue has 0 keys per batch");
  }

  // Test 2: Identical sprites coalesce into a single draw batch
  {
    RenderQueue queue(32);
    auto* dummyTex = reinterpret_cast<SDL_Texture*>(0x1000);
    constexpr int kCount = 10;
    for (int i = 0; i < kCount; ++i) {
      auto& sprite = queue.EmplaceSprite(0, 0.0f, dummyTex, octarine::BlendMode::Blend);
      sprite.texture = dummyTex;
    }
    queue.Sort();
    const auto stats = AnalyzeRenderQueue(queue);
    CheckEq(stats.commandCount, size_t{kCount}, "10 sprites registered");
    CheckEq(stats.drawBatches, size_t{1}, "All 10 identical sprites coalesce into 1 batch");
    CheckEq(stats.textureSwitches, size_t{0}, "0 texture switches");
    CheckEq(stats.blendSwitches, size_t{0}, "0 blend switches");
    CheckEq(stats.clipSwitches, size_t{0}, "0 clip switches");
    CheckEq(stats.topologySwitches, size_t{0}, "0 topology switches");
    CheckEq(stats.keysPerBatch, 10.0, "10 keys per batch");
    CheckEq(stats.totalVertices, size_t{40}, "10 sprites = 40 vertices (4 verts/sprite)");
    CheckEq(stats.totalIndices, size_t{60}, "10 sprites = 60 indices (6 indices/sprite)");
  }

  // Test 3: Texture switch breaks batch
  {
    RenderQueue queue(32);
    auto* texA = reinterpret_cast<SDL_Texture*>(0x1000);
    auto* texB = reinterpret_cast<SDL_Texture*>(0x2000);
    for (int i = 0; i < 4; ++i) {
      auto& sprite = queue.EmplaceSprite(0, 0.0f, texA);
      sprite.texture = texA;
    }
    for (int i = 0; i < 6; ++i) {
      auto& sprite = queue.EmplaceSprite(0, 0.0f, texB);
      sprite.texture = texB;
    }
    queue.Sort();
    const auto stats = AnalyzeRenderQueue(queue);
    CheckEq(stats.commandCount, size_t{10}, "10 total commands");
    CheckEq(stats.drawBatches, size_t{2}, "Switching texture produces 2 batches");
    CheckEq(stats.textureSwitches, size_t{1}, "Exactly 1 texture switch");
    CheckEq(stats.keysPerBatch, 5.0, "Average 5 keys per batch");
  }

  // Test 4: Blend mode switch breaks batch
  {
    RenderQueue queue(32);
    auto* tex = reinterpret_cast<SDL_Texture*>(0x1000);
    for (int i = 0; i < 3; ++i) {
      auto& sprite = queue.EmplaceSprite(0, 0.0f, tex, octarine::BlendMode::Blend);
      sprite.texture = tex;
      sprite.blendMode = SDL_BLENDMODE_BLEND;
    }
    for (int i = 0; i < 3; ++i) {
      auto& sprite = queue.EmplaceSprite(0, 0.0f, tex, octarine::BlendMode::Add);
      sprite.texture = tex;
      sprite.blendMode = SDL_BLENDMODE_ADD;
    }
    queue.Sort();
    const auto stats = AnalyzeRenderQueue(queue);
    CheckEq(stats.drawBatches, size_t{2}, "Blend mode change splits batch");
    CheckEq(stats.blendSwitches, size_t{1}, "1 blend mode switch");
  }

  // Test 5: Clip rect switch breaks batch
  {
    RenderQueue queue(32);
    auto* tex = reinterpret_cast<SDL_Texture*>(0x1000);
    const uint8_t clip1 = queue.RegisterClipRect(SDL_Rect{0, 0, 100, 100});
    const uint8_t clip2 = queue.RegisterClipRect(SDL_Rect{100, 100, 100, 100});

    for (int i = 0; i < 3; ++i) {
      auto& sprite = queue.EmplaceSprite(0, 0.0f, tex, octarine::BlendMode::Blend, clip1);
      sprite.texture = tex;
    }
    for (int i = 0; i < 3; ++i) {
      auto& sprite = queue.EmplaceSprite(0, 0.0f, tex, octarine::BlendMode::Blend, clip2);
      sprite.texture = tex;
    }
    queue.Sort();
    const auto stats = AnalyzeRenderQueue(queue);
    CheckEq(stats.drawBatches, size_t{2}, "Clip rect change splits batch");
    CheckEq(stats.clipSwitches, size_t{1}, "1 clip switch");
  }

  // Test 6: Topology switch (Lines vs Triangles)
  {
    RenderQueue queue(32);
    for (int i = 0; i < 2; ++i) {
      auto& line = queue.EmplaceLine(0, 0.0f);
      line.start = {0.0f, 0.0f};
      line.end = {100.0f, 0.0f};
      line.thickness = 1.0f;  // hairline = Lines topology
    }
    for (int i = 0; i < 2; ++i) {
      auto& sq = queue.EmplaceSquare(0, 0.0f);
      sq.destRect = {0.0f, 0.0f, 32.0f, 32.0f};
    }
    queue.Sort();
    const auto stats = AnalyzeRenderQueue(queue, 1.0f);
    CheckEq(stats.drawBatches, size_t{2}, "Lines then Squares produce 2 batches");
    CheckEq(stats.topologySwitches, size_t{1}, "1 topology switch");
    CheckEq(stats.totalVertices, size_t{12}, "2 lines * 2 verts + 2 squares * 4 verts = 12 verts");
  }

  // Test 7: Degenerate outline triangle falls back to Lines
  {
    RenderQueue queue(16);
    auto& tri = queue.EmplaceTriangle(0, 0.0f);
    tri.p1 = {0.0f, 0.0f};
    tri.p2 = {10.0f, 0.0f};
    tri.p3 = {20.0f, 0.0f};  // collinear -> zero area
    tri.thickness = 5.0f;
    tri.filled = false;
    queue.Sort();
    const auto stats = AnalyzeRenderQueue(queue, 1.0f);
    CheckEq(stats.commandCount, size_t{1}, "1 degenerate triangle registered");
    CheckEq(stats.drawBatches, size_t{1}, "1 batch for degenerate outline triangle");
    CheckEq(stats.totalVertices, size_t{4}, "degenerate outline triangle draws as 4-point line loop");
  }

  return octarine::test::ReportSummary("RenderQueueStatsTest");
}
