#include <benchmark/benchmark.h>

#include <vector>

#include "BenchCommon.h"
#include "ECS/Registry.h"
#include "Systems/EntityPoolSystem.h"

// Misc/Pool: entity lifecycle costs — raw create+blam, blam with relationship pairs (reverse-index
// cleanup), and pooled spawn/park recycling. Setup that must not be timed lives outside the
// timing loop or behind PauseTiming on large batches (PauseTiming itself costs ~hundreds of ns,
// so tiny batches would mostly measure the timer).

namespace {

struct Position {
  float x = 0.0f;
  float y = 0.0f;
};

struct Velocity {
  float dx = 0.0f;
  float dy = 0.0f;
};

}  // namespace

static void BM_EntityCreateAndBlam(benchmark::State& state) {
  const auto n = static_cast<int>(state.range(0));
  Registry registry;
  for (auto _ : state) {
    for (int i = 0; i < n; ++i) {
      const Entity e = registry.CreateEntityWithBundle(Position{1.0f, 1.0f}, Velocity{1.0f, 1.0f});
      registry.QueueBlamEntity(e);
    }
    registry.Update(bench::kDt);
  }
  bench::ReportPerEntity(state, n);
}
BENCHMARK(BM_EntityCreateAndBlam)->Name("Misc/Pool/CreateAndBlam")->RangeMultiplier(8)->Range(512, 32768);

static void BM_EntityCreateAndBlamWithPairs(benchmark::State& state) {
  const auto n = static_cast<int>(state.range(0));
  for (auto _ : state) {
    state.PauseTiming();
    Registry registry;
    std::vector<Entity> targets;
    targets.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) targets.push_back(registry.CreateEntity());
    const Entity relationship = registry.TagId("test_rel");
    for (int i = 0; i < n; ++i)
      registry.AddPair(registry.CreateEntity(), relationship, targets[static_cast<size_t>(i)]);
    state.ResumeTiming();

    for (const Entity e : targets) registry.QueueBlamEntity(e);
    registry.Update(bench::kDt);
  }
  bench::ReportPerEntity(state, n);
}
BENCHMARK(BM_EntityCreateAndBlamWithPairs)->Name("Misc/Pool/BlamWithPairs")->RangeMultiplier(8)->Range(512, 32768);

static void BM_EntityPoolSpawnAndPark(benchmark::State& state) {
  const auto n = static_cast<int>(state.range(0));
  Registry registry;
  EntityPoolManager pool;
  const ArchetypeID poolId = pool.RegisterPool<Position, Velocity>(registry, [](Registry& reg) {
    const Entity e = reg.CreateEntityWithBundle(Position{0.0f, 0.0f}, Velocity{0.0f, 0.0f});
    reg.AddTag<PoolableTag>(e);
    return e;
  });
  std::vector<Entity> entities;
  entities.reserve(static_cast<size_t>(n));
  // Pre-warm so the timed loop is pure activation/deactivation, never fresh allocation.
  for (int i = 0; i < n; ++i) entities.push_back(pool.Spawn(registry, poolId));
  for (const Entity e : entities) pool.Park(registry, e);

  for (auto _ : state) {
    entities.clear();
    for (int i = 0; i < n; ++i) entities.push_back(pool.Spawn(registry, poolId));
    for (const Entity e : entities) pool.Park(registry, e);
  }
  bench::ReportPerEntity(state, n * 2.0);
}
BENCHMARK(BM_EntityPoolSpawnAndPark)->Name("Misc/Pool/SpawnAndPark")->RangeMultiplier(8)->Range(512, 32768);
