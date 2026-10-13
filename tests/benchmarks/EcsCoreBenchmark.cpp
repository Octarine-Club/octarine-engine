#include <benchmark/benchmark.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "BenchCommon.h"
#include "ECS/CommandBuffer.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "Systems/TransformSystem.h"

// ECS/ tier: per-frame costs of the registry itself, independent of gameplay systems — iteration
// (serial / facade / parallel, kernel cost, fragmentation, archetype width), structural churn,
// archetype creation with live queries, random/cross-entity access, transform hierarchy shapes.

namespace {

struct CorePos {
  float x = 0.0f;
  float y = 0.0f;
};

struct CoreVel {
  float dx = 0.0f;
  float dy = 0.0f;
};

struct MarkedTag {};

// 64-byte filler component; distinct types per index so an archetype can carry K of them.
template <size_t I>
struct Pad64 {
  float data[16] = {};
};

// Spread N entities across `archetypes` distinct shapes (same components, different tags) so
// iteration benchmarks exercise the multi-archetype path rather than one giant archetype.
std::vector<Entity> PopulateSpread(Registry& registry, const int count, const int archetypes) {
  std::vector<Entity> tags;
  tags.reserve(static_cast<size_t>(archetypes));
  for (int a = 0; a < archetypes; ++a) tags.push_back(registry.TagId("spread_" + std::to_string(a)));
  std::vector<Entity> entities;
  entities.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    const Entity e = registry.CreateEntityWithBundle(CorePos{1.0f, 2.0f}, CoreVel{0.5f, 0.25f});
    registry.AddTag(e, tags[static_cast<size_t>(i % archetypes)]);
    entities.push_back(e);
  }
  return entities;
}

// Per-entity work of increasing cost: 0 = 2 adds (memory bound), 1 = normalize + sqrt (typical
// steering/homing math), 2 = ~40 dependent FLOPs (heavy behaviour). Finds the serial/parallel
// crossover that the fixed 512-entity ParallelForEach gate ignores.
inline void Kernel(const int cost, CorePos& p, const CoreVel& v) {
  if (cost == 0) {
    p.x += v.dx;
    p.y += v.dy;
    return;
  }
  const float len = std::sqrt(v.dx * v.dx + v.dy * v.dy) + 1e-3f;
  float nx = v.dx / len;
  float ny = v.dy / len;
  if (cost >= 2) {
    for (int k = 0; k < 10; ++k) {
      const float t = nx * 0.99f - ny * 0.01f;
      ny = ny * 0.99f + nx * 0.01f;
      nx = t / std::sqrt(t * t + ny * ny + 1e-6f);
    }
  }
  p.x += nx;
  p.y += ny;
}

}  // namespace

// --- Iteration ---------------------------------------------------------------------------------

static void BM_IterateSerial(benchmark::State& state) {
  Registry registry;
  const auto n = static_cast<int>(state.range(0));
  PopulateSpread(registry, n, 8);
  auto query = registry.CreateQuery<CorePos, CoreVel>();
  query->Update();
  for (auto _ : state) {
    query->ForEach([](const Entity /*e*/, CorePos& p, const CoreVel& v) {
      p.x += v.dx;
      p.y += v.dy;
    });
    benchmark::ClobberMemory();
  }
  bench::ReportPerEntity(state, n);
}
BENCHMARK(BM_IterateSerial)->Name("ECS/Iterate/Serial")->RangeMultiplier(8)->Range(512, 262144);

static void BM_IterateFacade(benchmark::State& state) {
  Registry registry;
  const auto n = static_cast<int>(state.range(0));
  PopulateSpread(registry, n, 8);
  auto query = registry.CreateQuery<CorePos, CoreVel>();
  query->Update();
  for (auto _ : state) {
    query->ForEach([](const ContextFacade& /*ctx*/, const Entity /*e*/, CorePos& p, const CoreVel& v) {
      p.x += v.dx;
      p.y += v.dy;
    });
    benchmark::ClobberMemory();
  }
  bench::ReportPerEntity(state, n);
}
BENCHMARK(BM_IterateFacade)->Name("ECS/Iterate/Facade")->RangeMultiplier(8)->Range(512, 262144);

// Args: {entities, kernel cost, parallel?}
static void BM_IterateKernelCost(benchmark::State& state) {
  Registry registry;
  const auto n = static_cast<int>(state.range(0));
  const auto cost = static_cast<int>(state.range(1));
  const bool parallel = state.range(2) != 0;
  PopulateSpread(registry, n, 8);
  auto query = registry.CreateQuery<CorePos, CoreVel>();
  query->Update();
  for (auto _ : state) {
    if (parallel) {
      query->ParallelForEach([cost](const Entity /*e*/, CorePos& p, const CoreVel& v) { Kernel(cost, p, v); });
    } else {
      query->ForEach([cost](const Entity /*e*/, CorePos& p, const CoreVel& v) { Kernel(cost, p, v); });
    }
    benchmark::ClobberMemory();
  }
  bench::ReportPerEntity(state, n);
}
BENCHMARK(BM_IterateKernelCost)
    ->Name("ECS/Iterate/KernelCost")
    ->ArgNames({"n", "cost", "parallel"})
    ->ArgsProduct({{1024, 4096, 16384, 65536, 262144}, {0, 1, 2}, {0, 1}})
    ->UseRealTime();

// Fixed N spread over increasingly many archetypes: per-archetype/per-chunk overhead and chunk
// under-fill (16 KB chunks hold far more than N/archetypes entities at the high end).
static void BM_IterateFragmentation(benchmark::State& state) {
  Registry registry;
  constexpr int kEntities = 65536;
  const auto archetypes = static_cast<int>(state.range(0));
  PopulateSpread(registry, kEntities, archetypes);
  auto query = registry.CreateQuery<CorePos, CoreVel>();
  query->Update();
  for (auto _ : state) {
    query->ForEach([](const Entity /*e*/, CorePos& p, const CoreVel& v) {
      p.x += v.dx;
      p.y += v.dy;
    });
    benchmark::ClobberMemory();
  }
  bench::ReportPerEntity(state, kEntities);
  bench::ReportRegistry(state, registry);
}
BENCHMARK(BM_IterateFragmentation)->Name("ECS/Iterate/Fragmentation")->RangeMultiplier(4)->Range(1, 4096);

// Iterate Pos+Vel inside archetypes that also carry K unrelated 64-byte components. Wider rows
// mean fewer entities per 16 KB chunk, so more chunk hops for the same N.
template <size_t... I>
void PopulateWide(Registry& registry, const int count, std::index_sequence<I...> /*pads*/) {
  for (int i = 0; i < count; ++i) {
    registry.CreateEntityWithBundle(CorePos{1.0f, 2.0f}, CoreVel{0.5f, 0.25f}, Pad64<I>{}...);
  }
}

template <size_t K>
static void BM_IterateArchetypeWidth(benchmark::State& state) {
  Registry registry;
  constexpr int kEntities = 65536;
  PopulateWide(registry, kEntities, std::make_index_sequence<K>{});
  auto query = registry.CreateQuery<CorePos, CoreVel>();
  query->Update();
  for (auto _ : state) {
    query->ForEach([](const Entity /*e*/, CorePos& p, const CoreVel& v) {
      p.x += v.dx;
      p.y += v.dy;
    });
    benchmark::ClobberMemory();
  }
  bench::ReportPerEntity(state, kEntities);
}
BENCHMARK(BM_IterateArchetypeWidth<0>)->Name("ECS/Iterate/ArchetypeWidth/extra_bytes:0");
BENCHMARK(BM_IterateArchetypeWidth<2>)->Name("ECS/Iterate/ArchetypeWidth/extra_bytes:128");
BENCHMARK(BM_IterateArchetypeWidth<4>)->Name("ECS/Iterate/ArchetypeWidth/extra_bytes:256");
BENCHMARK(BM_IterateArchetypeWidth<8>)->Name("ECS/Iterate/ArchetypeWidth/extra_bytes:512");

// --- Archetype creation under live queries -----------------------------------------------------

// Cost of minting one brand-new archetype (entity + unique tag) and re-matching Q live queries
// (≈ registered systems). Q=0 is the control and runs under the identical harness, so the delta
// between rows isolates the query re-match cascade. Fixed iteration count keeps the archetype
// population (1024 → 3072) comparable across rows.
static void BM_ArchetypeCreateWithQueries(benchmark::State& state) {
  Registry registry;
  PopulateSpread(registry, 1024, 1024);
  const auto queryCount = static_cast<int>(state.range(0));
  std::vector<std::unique_ptr<ComponentQuery<CorePos, CoreVel>>> queries;
  for (int q = 0; q < queryCount; ++q) {
    queries.push_back(registry.CreateQuery<CorePos, CoreVel>());
    queries.back()->Update();
  }
  int counter = 0;
  std::vector<Entity> tags;
  tags.reserve(static_cast<size_t>(state.max_iterations));
  for (int64_t i = 0; i < state.max_iterations; ++i) tags.push_back(registry.TagId("churn_" + std::to_string(i)));

  for (auto _ : state) {
    const Entity e = registry.CreateEntityWithBundle(CorePos{}, CoreVel{});
    registry.AddTag(e, tags[static_cast<size_t>(counter++)]);
    for (auto& query : queries) query->Update();
  }
  bench::ReportRegistry(state, registry);
}
BENCHMARK(BM_ArchetypeCreateWithQueries)
    ->Name("ECS/Churn/NewArchetype")
    ->ArgName("queries")
    ->Arg(0)
    ->Arg(16)
    ->Arg(64)
    ->Iterations(2048)
    ->Repetitions(3)
    ->ReportAggregatesOnly(true);

// --- Structural changes --------------------------------------------------------------------------

// Hot-edge cycling: after the first lap both archetypes and their add/remove edges exist, so
// this measures the steady-state move cost (CopyComponents + swap-with-last + location patch).
static void BM_ArchetypeTransitionAddRemove(benchmark::State& state) {
  Registry registry;
  std::vector<Entity> entities;
  const auto n = static_cast<int>(state.range(0));
  entities.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) entities.push_back(registry.CreateEntityWithBundle(CorePos{}));

  for (auto _ : state) {
    for (const Entity e : entities) registry.AddComponent(e, CoreVel{1.0f, 1.0f});
    for (const Entity e : entities) registry.RemoveComponent<CoreVel>(e);
  }
  bench::ReportPerEntity(state, n * 2.0);
}
BENCHMARK(BM_ArchetypeTransitionAddRemove)->Name("ECS/Structural/AddRemove")->RangeMultiplier(8)->Range(512, 32768);

// Steady-state spawn/despawn churn like a survivors wave: every frame `pct`% of a population of
// enemy-width entities dies (QueueBlamEntity) and the same number spawn via bundle; the flush runs
// inside Registry::Update. Args: {entities, pct}.
static void BM_SpawnDespawnPerFrame(benchmark::State& state) {
  Registry registry;
  const auto n = static_cast<int>(state.range(0));
  const auto perFrame = std::max(1, static_cast<int>(state.range(0) * state.range(1) / 100));
  std::vector<Entity> live;
  live.reserve(static_cast<size_t>(n));
  const auto spawn = [&] {
    return registry.CreateEntityWithBundle(CorePos{}, CoreVel{}, Pad64<0>{}, Pad64<1>{}, Pad64<2>{});
  };
  for (int i = 0; i < n; ++i) live.push_back(spawn());
  size_t cursor = 0;

  for (auto _ : state) {
    for (int i = 0; i < perFrame; ++i) {
      registry.QueueBlamEntity(live[cursor]);
      live[cursor] = spawn();
      cursor = (cursor + 1) % live.size();
    }
    registry.Update(bench::kDt);
  }
  bench::ReportPerEntity(state, perFrame);
  state.counters["churn_per_frame"] = perFrame;
}
BENCHMARK(BM_SpawnDespawnPerFrame)
    ->Name("ECS/Structural/SpawnDespawnPerFrame")
    ->ArgNames({"n", "pct"})
    ->ArgsProduct({{10000, 50000}, {1, 5, 20}});

// Deferred tag toggling through a CommandBuffer (the parallel-system structural-change path):
// 10% of entities gain a tag one frame and lose it the next.
static void BM_CommandBufferTagToggle(benchmark::State& state) {
  Registry registry;
  const auto n = static_cast<int>(state.range(0));
  auto entities = PopulateSpread(registry, n, 1);
  CommandBuffer buffer;
  const size_t stride = 10;
  for (auto _ : state) {
    for (size_t i = 0; i < entities.size(); i += stride) buffer.AddTag<MarkedTag>(entities[i]);
    buffer.Playback(&registry);
    for (size_t i = 0; i < entities.size(); i += stride) buffer.RemoveTag<MarkedTag>(entities[i]);
    buffer.Playback(&registry);
  }
  const size_t toggledCount = entities.size() / stride;
  bench::ReportPerEntity(state, static_cast<double>(toggledCount) * 2.0);
}
BENCHMARK(BM_CommandBufferTagToggle)->Name("ECS/Structural/CommandBufferTagToggle")->Arg(10000)->Arg(50000);

// --- Access patterns -----------------------------------------------------------------------------

static void BM_GetComponentRandomAccess(benchmark::State& state) {
  Registry registry;
  const auto n = static_cast<int>(state.range(0));
  auto entities = PopulateSpread(registry, n, 8);
  // NOLINTNEXTLINE(cert-msc51-cpp) — fixed seed keeps the access pattern identical across runs
  std::mt19937 rng(42);
  std::shuffle(entities.begin(), entities.end(), rng);

  for (auto _ : state) {
    float sum = 0.0f;
    for (const Entity e : entities) sum += registry.GetComponent<CorePos>(e).x;
    benchmark::DoNotOptimize(sum);
  }
  bench::ReportPerEntity(state, n);
}
BENCHMARK(BM_GetComponentRandomAccess)->Name("ECS/Access/RandomGet")->RangeMultiplier(8)->Range(512, 262144);

// The pattern every steering/homing/cull system uses per entity: IsAlive(target) +
// HasComponent(target) + GetComponent(target) on one shared target (the player).
static void BM_TargetLookup(benchmark::State& state) {
  Registry registry;
  const auto n = static_cast<int>(state.range(0));
  PopulateSpread(registry, n, 8);
  const Entity target = registry.CreateEntityWithBundle(CorePos{5.0f, 5.0f});
  auto query = registry.CreateQuery<CorePos, CoreVel>();
  query->Update();
  for (auto _ : state) {
    query->ForEach([&](const Entity /*e*/, CorePos& p, const CoreVel& /*v*/) {
      if (!registry.IsAlive(target) || !registry.HasComponent<CorePos>(target)) return;
      const auto& t = registry.GetComponent<CorePos>(target);
      p.x += (t.x - p.x) * 0.01f;
      p.y += (t.y - p.y) * 0.01f;
    });
    benchmark::ClobberMemory();
  }
  bench::ReportPerEntity(state, n);
}
BENCHMARK(BM_TargetLookup)->Name("ECS/Access/SharedTargetLookup")->RangeMultiplier(8)->Range(512, 262144);

// --- TransformSystem hierarchy shapes ---------------------------------------------------------

namespace {

void AddFlatTransforms(Registry& registry, const int count) {
  for (int i = 0; i < count; ++i) {
    registry.CreateEntityWithBundle(GlobalTransformComponent{},
                                    PositionComponent{glm::vec2(static_cast<float>(i), 0.0f)});
  }
}

Entity AddTransform(Registry& registry) {
  return registry.CreateEntityWithBundle(GlobalTransformComponent{}, PositionComponent{glm::vec2(1.0f, 1.0f)});
}

}  // namespace

// Args: {flat entities, shape} where shape 0 = flat only, 1 = one 16-deep chain on top,
// 2 = N/8 chains of depth 8 (everything hierarchical), 3 = 64 parents with N/64 children each.
static void BM_TransformShapes(benchmark::State& state) {
  Registry registry;
  registry.RegisterBulkSystem<GlobalTransformComponent>(TransformSystem());
  const auto n = static_cast<int>(state.range(0));
  const auto shape = state.range(1);
  if (shape == 0 || shape == 1) AddFlatTransforms(registry, n);
  if (shape == 1) {
    Entity parent = AddTransform(registry);
    for (int i = 1; i < 16; ++i) {
      const Entity child = AddTransform(registry);
      registry.SetParent(child, parent);
      parent = child;
    }
  }
  if (shape == 2) {
    for (int c = 0; c < n / 8; ++c) {
      Entity parent = AddTransform(registry);
      for (int d = 1; d < 8; ++d) {
        const Entity child = AddTransform(registry);
        registry.SetParent(child, parent);
        parent = child;
      }
    }
  }
  if (shape == 3) {
    for (int p = 0; p < 64; ++p) {
      const Entity parent = AddTransform(registry);
      for (int c = 0; c < n / 64; ++c) registry.SetParent(AddTransform(registry), parent);
    }
  }

  for (auto _ : state) registry.Update(bench::kDt);
  bench::ReportPerEntity(state, n);
  bench::ReportRegistry(state, registry);
}
BENCHMARK(BM_TransformShapes)
    ->Name("ECS/Transform")
    ->ArgNames({"n", "shape"})
    ->ArgsProduct({{4096, 32768, 131072}, {0, 1, 2, 3}});

// --- Entity creation -------------------------------------------------------------------------

static void BM_CreateEntityWithBundle(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    Registry registry;
    state.ResumeTiming();
    for (int i = 0; i < state.range(0); ++i) registry.CreateEntityWithBundle(CorePos{1.0f, 1.0f}, CoreVel{1.0f, 1.0f});
  }
  bench::ReportPerEntity(state, static_cast<double>(state.range(0)));
}
BENCHMARK(BM_CreateEntityWithBundle)->Name("ECS/Create/Bundle")->RangeMultiplier(8)->Range(4096, 262144);

static void BM_CreateEntityAddChain(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    Registry registry;
    state.ResumeTiming();
    for (int i = 0; i < state.range(0); ++i) {
      const Entity e = registry.CreateEntity();
      registry.AddComponent(e, CorePos{1.0f, 1.0f});
      registry.AddComponent(e, CoreVel{1.0f, 1.0f});
    }
  }
  bench::ReportPerEntity(state, static_cast<double>(state.range(0)));
}
BENCHMARK(BM_CreateEntityAddChain)->Name("ECS/Create/AddChain")->RangeMultiplier(8)->Range(4096, 262144);
