#include <benchmark/benchmark.h>

#include <vector>

#include "BenchCommon.h"
#include "General/BlendMode.h"
#include "Renderer/RenderQueue.h"

namespace {

void BM_RenderQueue_SingleAtlas(benchmark::State& state) {
  const auto n = static_cast<size_t>(state.range(0));
  RenderQueue queue(n + 16);
  auto* dummyTex = reinterpret_cast<SDL_Texture*>(0x1000);

  // Warm-up pass to populate internal scratch vectors without polluting AllocScope.
  queue.Clear();
  for (size_t i = 0; i < n; ++i) {
    auto& sprite = queue.EmplaceSprite(0, 10.0f, dummyTex);
    sprite.texture = dummyTex;
  }
  queue.Sort();

  bench::AllocScope allocScope;
  bench::HwCounterScope hwScope(static_cast<double>(n));

  for (auto _ : state) {
    queue.Clear();
    for (size_t i = 0; i < n; ++i) {
      auto& sprite = queue.EmplaceSprite(0, 10.0f, dummyTex);
      sprite.texture = dummyTex;
    }
    queue.Sort();
    benchmark::DoNotOptimize(queue.Size());
  }

  allocScope.Stop();
  hwScope.Stop();

  bench::ReportPerEntity(state, static_cast<double>(n));
  bench::ReportRenderQueue(state, queue);
  allocScope.Report(state);
  hwScope.Report(state);
}
BENCHMARK(BM_RenderQueue_SingleAtlas)
    ->Name("Sys/RenderQueue/BatchCoalescing/SingleAtlas")
    ->RangeMultiplier(4)
    ->Range(256, 16384);

void BM_RenderQueue_MultiTextureYSorted(benchmark::State& state) {
  const auto n = static_cast<size_t>(state.range(0));
  RenderQueue queue(n + 16);
  constexpr size_t kTextureCount = 16;
  std::vector<SDL_Texture*> textures;
  textures.reserve(kTextureCount);
  for (size_t i = 0; i < kTextureCount; ++i) {
    textures.push_back(reinterpret_cast<SDL_Texture*>(0x1000 + i * 0x100));
  }

  // Warm-up pass to populate internal scratch vectors without polluting AllocScope.
  queue.Clear();
  for (size_t i = 0; i < n; ++i) {
    auto* tex = textures[i % kTextureCount];
    const auto y = static_cast<float>((i * 37) % 2000 - 1000);
    auto& sprite = queue.EmplaceSprite(0, y, tex);
    sprite.texture = tex;
  }
  queue.Sort();

  bench::AllocScope allocScope;
  bench::HwCounterScope hwScope(static_cast<double>(n));

  for (auto _ : state) {
    queue.Clear();
    for (size_t i = 0; i < n; ++i) {
      auto* tex = textures[i % kTextureCount];
      const auto y = static_cast<float>((i * 37) % 2000 - 1000);
      auto& sprite = queue.EmplaceSprite(0, y, tex);
      sprite.texture = tex;
    }
    queue.Sort();
    benchmark::DoNotOptimize(queue.Size());
  }

  allocScope.Stop();
  hwScope.Stop();

  bench::ReportPerEntity(state, static_cast<double>(n));
  bench::ReportRenderQueue(state, queue);
  allocScope.Report(state);
  hwScope.Report(state);
}
BENCHMARK(BM_RenderQueue_MultiTextureYSorted)
    ->Name("Sys/RenderQueue/BatchCoalescing/MultiTextureYSorted")
    ->RangeMultiplier(4)
    ->Range(256, 16384);

void BM_RenderQueue_MixedPrimitives(benchmark::State& state) {
  const auto n = static_cast<size_t>(state.range(0));
  RenderQueue queue(n + 16);
  auto* dummyTex = reinterpret_cast<SDL_Texture*>(0x1000);

  // Warm-up pass to populate internal scratch vectors without polluting AllocScope.
  queue.Clear();
  for (size_t i = 0; i < n; ++i) {
    const auto mode = i % 4;
    if (mode == 0) {
      auto& sprite = queue.EmplaceSprite(0, 0.0f, dummyTex);
      sprite.texture = dummyTex;
    } else if (mode == 1) {
      auto& sq = queue.EmplaceSquare(0, 0.0f);
      sq.destRect = {0.0f, 0.0f, 16.0f, 16.0f};
    } else if (mode == 2) {
      auto& line = queue.EmplaceLine(0, 0.0f);
      line.start = {0.0f, 0.0f};
      line.end = {16.0f, 16.0f};
      line.thickness = 1.0f;
    } else {
      auto& circ = queue.EmplaceCircle(0, 0.0f);
      circ.radiusX = 8.0f;
      circ.radiusY = 8.0f;
    }
  }
  queue.Sort();

  bench::AllocScope allocScope;
  bench::HwCounterScope hwScope(static_cast<double>(n));

  for (auto _ : state) {
    queue.Clear();
    for (size_t i = 0; i < n; ++i) {
      const auto mode = i % 4;
      if (mode == 0) {
        auto& sprite = queue.EmplaceSprite(0, 0.0f, dummyTex);
        sprite.texture = dummyTex;
      } else if (mode == 1) {
        auto& sq = queue.EmplaceSquare(0, 0.0f);
        sq.destRect = {0.0f, 0.0f, 16.0f, 16.0f};
      } else if (mode == 2) {
        auto& line = queue.EmplaceLine(0, 0.0f);
        line.start = {0.0f, 0.0f};
        line.end = {16.0f, 16.0f};
        line.thickness = 1.0f;
      } else {
        auto& circ = queue.EmplaceCircle(0, 0.0f);
        circ.radiusX = 8.0f;
        circ.radiusY = 8.0f;
      }
    }
    queue.Sort();
    benchmark::DoNotOptimize(queue.Size());
  }

  allocScope.Stop();
  hwScope.Stop();

  bench::ReportPerEntity(state, static_cast<double>(n));
  bench::ReportRenderQueue(state, queue);
  allocScope.Report(state);
  hwScope.Report(state);
}
BENCHMARK(BM_RenderQueue_MixedPrimitives)
    ->Name("Sys/RenderQueue/BatchCoalescing/MixedPrimitives")
    ->RangeMultiplier(4)
    ->Range(256, 16384);

}  // namespace
