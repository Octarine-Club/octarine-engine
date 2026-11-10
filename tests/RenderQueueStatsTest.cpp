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

  // Test 8: Text command classification
  {
    RenderQueue queue(16);
    auto* dummyTex = reinterpret_cast<SDL_Texture*>(0x3000);
    auto& text = queue.EmplaceText(0, 0.0f, dummyTex);
    text.texture = dummyTex;
    queue.Sort();
    const auto stats = AnalyzeRenderQueue(queue, 1.0f);
    CheckEq(stats.commandCount, size_t{1}, "1 text command registered");
    CheckEq(stats.drawBatches, size_t{1}, "1 batch for text command");
    CheckEq(stats.totalVertices, size_t{4}, "Text command uses quad (4 vertices)");
    CheckEq(stats.totalIndices, size_t{6}, "Text command uses 6 indices");
  }

  // Test 9: Thick line and degenerate line
  {
    RenderQueue queue(16);
    auto& thickLine = queue.EmplaceLine(0, 0.0f);
    thickLine.start = {0.0f, 0.0f};
    thickLine.end = {50.0f, 50.0f};
    thickLine.thickness = 4.0f;

    auto& degenLine = queue.EmplaceLine(0, 1.0f);
    degenLine.start = {10.0f, 10.0f};
    degenLine.end = {10.0f, 10.0f};  // zero length
    degenLine.thickness = 4.0f;

    queue.Sort();
    const auto stats = AnalyzeRenderQueue(queue, 1.0f);
    CheckEq(stats.commandCount, size_t{1}, "1 valid line registered (degenerate skipped)");
    CheckEq(stats.totalVertices, size_t{4}, "Thick line produces 4 vertices, degen 0");
  }

  // Test 10: Circle classification (filled, hairline outline, thick outline, degenerate)
  {
    RenderQueue queue(16);
    auto& filledCirc = queue.EmplaceCircle(0, 0.0f);
    filledCirc.radiusX = 20.0f;
    filledCirc.radiusY = 20.0f;
    filledCirc.filled = true;

    auto& hairCirc = queue.EmplaceCircle(0, 1.0f);
    hairCirc.radiusX = 20.0f;
    hairCirc.radiusY = 20.0f;
    hairCirc.thickness = 1.0f;
    hairCirc.filled = false;

    auto& thickCirc = queue.EmplaceCircle(0, 2.0f);
    thickCirc.radiusX = 20.0f;
    thickCirc.radiusY = 20.0f;
    thickCirc.thickness = 4.0f;
    thickCirc.filled = false;

    auto& degenCirc = queue.EmplaceCircle(0, 3.0f);
    degenCirc.radiusX = 0.0f;
    degenCirc.radiusY = 0.0f;

    queue.Sort();
    const auto stats = AnalyzeRenderQueue(queue, 1.0f);
    CheckEq(stats.commandCount, size_t{3}, "3 valid circles registered (degenerate skipped)");
    Check(stats.totalVertices > 0, "Circles produce vertices");
  }

  // Test 11: Triangle classification (filled, hairline outline, thick outline)
  {
    RenderQueue queue(16);
    auto& filledTri = queue.EmplaceTriangle(0, 0.0f);
    filledTri.p1 = {0.0f, 0.0f};
    filledTri.p2 = {20.0f, 0.0f};
    filledTri.p3 = {10.0f, 20.0f};
    filledTri.filled = true;

    auto& hairTri = queue.EmplaceTriangle(0, 1.0f);
    hairTri.p1 = {0.0f, 0.0f};
    hairTri.p2 = {20.0f, 0.0f};
    hairTri.p3 = {10.0f, 20.0f};
    hairTri.thickness = 1.0f;
    hairTri.filled = false;

    auto& thickTri = queue.EmplaceTriangle(0, 2.0f);
    thickTri.p1 = {0.0f, 0.0f};
    thickTri.p2 = {20.0f, 0.0f};
    thickTri.p3 = {10.0f, 20.0f};
    thickTri.thickness = 4.0f;
    thickTri.filled = false;

    queue.Sort();
    const auto stats = AnalyzeRenderQueue(queue, 1.0f);
    CheckEq(stats.commandCount, size_t{3}, "3 triangles registered");
    CheckEq(stats.drawBatches, size_t{3}, "Triangles with different topologies split batches");
  }

  // Test 12: Capsule classification (filled, hairline, thick, degen radius, degen points)
  {
    RenderQueue queue(16);
    auto& filledCap = queue.EmplaceCapsule(0, 0.0f);
    filledCap.start = {0.0f, 0.0f};
    filledCap.end = {40.0f, 0.0f};
    filledCap.radius = 10.0f;
    filledCap.filled = true;

    auto& hairCap = queue.EmplaceCapsule(0, 1.0f);
    hairCap.start = {0.0f, 0.0f};
    hairCap.end = {40.0f, 0.0f};
    hairCap.radius = 10.0f;
    hairCap.thickness = 1.0f;
    hairCap.filled = false;

    auto& thickCap = queue.EmplaceCapsule(0, 2.0f);
    thickCap.start = {0.0f, 0.0f};
    thickCap.end = {40.0f, 0.0f};
    thickCap.radius = 10.0f;
    thickCap.thickness = 4.0f;
    thickCap.filled = false;

    auto& degenRadiusCap = queue.EmplaceCapsule(0, 3.0f);
    degenRadiusCap.start = {0.0f, 0.0f};
    degenRadiusCap.end = {40.0f, 0.0f};
    degenRadiusCap.radius = 0.0f;  // falls back to line

    auto& degenPointsCap = queue.EmplaceCapsule(0, 4.0f);
    degenPointsCap.start = {10.0f, 10.0f};
    degenPointsCap.end = {10.0f, 10.0f};  // falls back to circle
    degenPointsCap.radius = 10.0f;

    queue.Sort();
    const auto stats = AnalyzeRenderQueue(queue, 1.0f);
    CheckEq(stats.commandCount, size_t{5}, "5 capsules registered");
    Check(stats.totalVertices > 0, "Capsules produce geometry");
  }

  // Test 13: TileChunk classification (valid, degenerate)
  {
    RenderQueue queue(16);
    auto* dummyTex = reinterpret_cast<SDL_Texture*>(0x4000);
    const SDL_Vertex verts[4]{};
    const int indices[6]{0, 1, 2, 2, 3, 0};

    auto& validChunk = queue.EmplaceTileChunk(0, 0.0f, dummyTex);
    validChunk.texture = dummyTex;
    validChunk.vertices = verts;
    validChunk.indices = indices;
    validChunk.vertexCount = 4;
    validChunk.indexCount = 6;

    auto& nullChunk = queue.EmplaceTileChunk(0, 1.0f);
    nullChunk.vertices = nullptr;
    nullChunk.vertexCount = 0;

    queue.Sort();
    const auto stats = AnalyzeRenderQueue(queue, 1.0f);
    CheckEq(stats.commandCount, size_t{1}, "1 valid tile chunk registered (null skipped)");
    CheckEq(stats.totalVertices, size_t{4}, "Only valid chunk contributes 4 vertices");
    CheckEq(stats.totalIndices, size_t{6}, "Only valid chunk contributes 6 indices");
  }

  return octarine::test::ReportSummary("RenderQueueStatsTest");
}
