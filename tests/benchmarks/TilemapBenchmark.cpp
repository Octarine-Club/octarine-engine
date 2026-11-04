#include <benchmark/benchmark.h>

#include <sstream>
#include <string>

#include "BenchCommon.h"
#include "Components/CameraComponents.h"
#include "ECS/Registry.h"
#include "Game/GameConfig.h"
#include "Renderer/RenderQueue.h"
#include "Systems/TilemapSystem.h"

namespace {

// Builds a synthetic 32x32-tile (512x512px) LDtk JSON level with dense Ground_Base + Ground_Overlay
// static meshes, 64 Y-sorted prop tiles, and a bordered/patterned Collisions IntGrid.
std::string BuildDenseBenchLdtkJson() {
  std::ostringstream os;
  os << R"({"defaultGridSize":16,"defs":{"tilesets":[{"uid":1,"identifier":"BenchTileset",)"
     << R"("relPath":"textures/bench.png","pxWid":256,"pxHei":256,"tileGridSize":16}]},)"
     << R"("levels":[{"identifier":"BenchChunk_0_0","worldX":0,"worldY":0,"pxWid":512,"pxHei":512,)"
     << R"("layerInstances":[)";

  // Props_YSort layer (64 tiles)
  os << R"({"__identifier":"Props_YSort","__type":"Tiles","__gridSize":16,"__tilesetDefUid":1,"gridTiles":[)";
  for (int i = 0; i < 64; ++i) {
    if (i > 0) os << ",";
    const int px = (i % 16) * 32;
    const int py = (i / 16) * 128;
    os << R"({"px":[)" << px << "," << py << R"(],"src":[0,0],"f":0})";
  }
  os << "]},";

  // Ground_Overlay layer (256 tiles)
  os << R"({"__identifier":"Ground_Overlay","__type":"AutoLayer","__gridSize":16,"__tilesetDefUid":1,"autoLayerTiles":[)";
  for (int i = 0; i < 256; ++i) {
    if (i > 0) os << ",";
    const int px = (i % 16) * 32;
    const int py = (i / 16) * 32;
    os << R"({"px":[)" << px << "," << py << R"(],"src":[16,0],"f":1})";
  }
  os << "]},";

  // Ground_Base layer (1024 tiles = full 32x32)
  os << R"({"__identifier":"Ground_Base","__type":"Tiles","__gridSize":16,"__tilesetDefUid":1,"gridTiles":[)";
  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 32; ++x) {
      if (x > 0 || y > 0) os << ",";
      os << R"({"px":[)" << (x * 16) << "," << (y * 16) << R"(],"src":[0,0],"f":0})";
    }
  }
  os << "]},";

  // Collisions IntGrid (32x32 = 1024 cells)
  os << R"({"__identifier":"Collisions","__type":"IntGrid","__cWid":32,"__cHei":32,"__gridSize":16,"intGridCsv":[)";
  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 32; ++x) {
      if (x > 0 || y > 0) os << ",";
      const int val = (x == 0 || y == 0 || (x % 8 == 4 && y % 8 == 4)) ? 1 : 0;
      os << val;
    }
  }
  os << "]}]}]}";
  return os.str();
}

void SetupBenchTilemap(Registry& registry, TilemapSystem& sys) {
  GameConfig cfg;
  cfg.windowWidth = 1920;
  cfg.windowHeight = 1080;
  registry.Set<GameConfig>(cfg);
  registry.Set<CameraComponent>(CameraComponent{octarine::Rect{-256.0f, -256.0f, 1920.0f, 1080.0f}, 1.0f});
  registry.Set<RenderQueue>(RenderQueue(8192));

  octarine::tilemap::TilemapLoadOptions opts;
  opts.stitchMode = octarine::tilemap::StitchMode::Toroidal;
  opts.loadRadius = 1;
  opts.unloadRadius = 2;
  const std::string json = BuildDenseBenchLdtkJson();
  sys.LoadFromJsonString(registry, json, opts);
}

}  // namespace

static void BM_TilemapRenderTick3x3(benchmark::State& state) {
  Registry registry;
  TilemapSystem sys;
  SetupBenchTilemap(registry, sys);
  auto& rq = registry.Get<RenderQueue>();

  for (auto _ : state) {
    rq.Clear();
    sys.Tick(&registry);
    benchmark::DoNotOptimize(rq.Size());
  }
  // 9 active chunks * (1024 base + 256 overlay + 64 ysort) = 12,096 tiles processed per frame.
  bench::ReportPerEntity(state, 9.0 * (1024 + 256 + 64));
}
BENCHMARK(BM_TilemapRenderTick3x3)->Name("Sys/Tilemap/RenderTick3x3");

static void BM_TilemapIsSolidAtBatch(benchmark::State& state) {
  const auto n = static_cast<int>(state.range(0));
  Registry registry;
  TilemapSystem sys;
  SetupBenchTilemap(registry, sys);

  for (auto _ : state) {
    int solidHits = 0;
    for (int i = 0; i < n; ++i) {
      const auto wx = static_cast<float>((i * 37) % 2048 - 1024);
      const auto wy = static_cast<float>((i * 53) % 2048 - 1024);
      solidHits += sys.IsSolidAt(wx, wy) ? 1 : 0;
    }
    benchmark::DoNotOptimize(solidHits);
  }
  bench::ReportPerEntity(state, n);
}
BENCHMARK(BM_TilemapIsSolidAtBatch)->Name("Sys/Tilemap/IsSolidAtBatch")->RangeMultiplier(4)->Range(1024, 16384);

static void BM_TilemapRaycastBatch(benchmark::State& state) {
  const auto n = static_cast<int>(state.range(0));
  Registry registry;
  TilemapSystem sys;
  SetupBenchTilemap(registry, sys);

  for (auto _ : state) {
    int hits = 0;
    for (int i = 0; i < n; ++i) {
      const float x0 = static_cast<float>((i * 29) % 1024 - 512) + 24.0f;
      const float y0 = static_cast<float>((i * 43) % 1024 - 512) + 24.0f;
      const auto res = sys.Raycast(x0, y0, x0 + 192.0f, y0 + 96.0f);
      hits += res.hit ? 1 : 0;
    }
    benchmark::DoNotOptimize(hits);
  }
  bench::ReportPerEntity(state, n);
}
BENCHMARK(BM_TilemapRaycastBatch)->Name("Sys/Tilemap/RaycastBatch")->RangeMultiplier(4)->Range(256, 4096);

static void BM_TilemapResolveCircleBatch(benchmark::State& state) {
  const auto n = static_cast<int>(state.range(0));
  Registry registry;
  TilemapSystem sys;
  SetupBenchTilemap(registry, sys);

  for (auto _ : state) {
    float accX = 0.0f;
    for (int i = 0; i < n; ++i) {
      const auto wx = static_cast<float>((i * 19) % 1024 - 512);
      const auto wy = static_cast<float>((i * 31) % 1024 - 512);
      const glm::vec2 resolved = sys.ResolveCircle(wx, wy, 10.0f);
      accX += resolved.x;
    }
    benchmark::DoNotOptimize(accX);
  }
  bench::ReportPerEntity(state, n);
}
BENCHMARK(BM_TilemapResolveCircleBatch)->Name("Sys/Tilemap/ResolveCircleBatch")->RangeMultiplier(4)->Range(1024, 16384);
