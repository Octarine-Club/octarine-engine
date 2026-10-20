#include <benchmark/benchmark.h>

#include <cstdint>
#include <glm/glm.hpp>
#include <stdexcept>

#include "Components/BoxColliderComponent.h"
#include "Components/CapsuleColliderComponent.h"
#include "Components/CircleColliderComponent.h"
#include "Components/EntityMaskComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/LineColliderComponent.h"
#include "Components/TriangleColliderComponent.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "Engine/EngineContext.h"
#include "EventBus/EventBus.h"
#include "Events/CollisionBatchEvent.h"
#include "Systems/KDTreeCollisionSystem.h"

// Fixed per-cycle overhead of KDTreeCollisionSystem's async path: each timed iteration drives one
// complete dispatch (ThreadPool submit) -> poll -> collect cycle on a trivially small box set, so
// what remains is scheduling latency rather than detection work. Detection at gameplay scale
// (thousands of moving boxes, several layouts) lives in Sys/Collision/Sync and Sys/Collision/Async.

namespace {

struct CollisionCounter {
  std::uint64_t count = 0;
  // KDTreeCollisionSystem emits exactly one batched event per collect cycle, so counting events (not
  // pairs) advances once per cycle — what the timing loop below keys on. Counting pairs would
  // hang: only *entering* pairs are emitted, so a sustained overlap reports its pair on the
  // first cycle and every later batch is empty.
  void OnCollisionBatch(const CollisionBatchEvent& /*e*/) { ++count; }
};

// Two boxes overlap at the origin so the narrowphase has at least one hit to process; the rest sit
// on a wide diagonal grid, non-overlapping, to keep narrowphase cheap and the dispatch overhead
// visible. (The overlap is sustained, so after the first cycle it is deduplicated out of the
// entering-pairs batches — the timing loop counts batch events, not pairs.)
void BuildBoxes(Registry& registry, int n) {
  EntityMask mask;
  mask.set(0);
  for (int i = 0; i < n; ++i) {
    Entity e = registry.CreateEntity();
    const float p = (i < 2) ? 0.0f : static_cast<float>(i) * 64.0f;
    registry.AddComponent(e, GlobalTransformComponent{glm::vec2(p, p), glm::vec2(1.0f, 1.0f), 0.0});
    registry.AddComponent(e, BoxColliderComponent(32, 32, glm::vec2(0.0f, 0.0f), false, mask));
    registry.AddComponent(e, EntityMaskComponent(mask));
  }
}
}  // namespace

static void BM_CollisionDispatch(benchmark::State& state) {
  Registry registry;
  EventBus bus;
  CollisionCounter counter;
  auto subscription =
      bus.SubscribeEvent<CollisionCounter, CollisionBatchEvent>(&counter, &CollisionCounter::OnCollisionBatch);

  EngineContext ec;
  ec.eventBus = &bus;
  registry.Set<EngineContext>(ec);

  BuildBoxes(registry, static_cast<int>(state.range(0)));

  KDTreeCollisionSystem system;
  ContextFacade ctx(&registry, 0.016f, nullptr, Internal::BulkGetComponent);

  // Each timed iteration advances exactly one full dispatch->collect cycle: the system polls the
  // outstanding future (cheap early-returns) until it is ready, collects + emits (bumping the
  // counter), and re-dispatches for the next iteration.
  for (auto _ : state) {
    const std::uint64_t before = counter.count;
    do {
      system(ctx);
    } while (counter.count == before);
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_CollisionDispatch)->Name("Sys/Collision/DispatchOverhead")->Range(8, 512)->UseRealTime();

namespace {
// Dense cluster: every box overlaps every other (all within a 16px span, boxes are 32px), so the
// detection pass yields O(n^2) *sustained* pairs. After the first cycle no pairs enter or exit, but
// the per-frame event-emission bookkeeping still runs over the full pair set every cycle:
// currentSet construction, the entering-pair scan, and the exiting-pair scan +
// CollisionExitBatchEvent emit. This isolates that bookkeeping at a realistic high pair count.
void BuildDenseCluster(Registry& registry, int n) {
  EntityMask mask;
  mask.set(0);
  for (int i = 0; i < n; ++i) {
    Entity e = registry.CreateEntity();
    const auto p = static_cast<float>(i % 8);  // all within [0,8) px -> mutual overlap
    registry.AddComponent(e, GlobalTransformComponent{glm::vec2(p, p), glm::vec2(1.0f, 1.0f), 0.0});
    registry.AddComponent(e, BoxColliderComponent(32, 32, glm::vec2(0.0f, 0.0f), false, mask));
    registry.AddComponent(e, EntityMaskComponent(mask));
  }
}
}  // namespace

static void BM_CollisionDispatchDense(benchmark::State& state) {
  Registry registry;
  EventBus bus;
  CollisionCounter counter;
  auto subscription =
      bus.SubscribeEvent<CollisionCounter, CollisionBatchEvent>(&counter, &CollisionCounter::OnCollisionBatch);

  EngineContext ec;
  ec.eventBus = &bus;
  registry.Set<EngineContext>(ec);

  BuildDenseCluster(registry, static_cast<int>(state.range(0)));

  KDTreeCollisionSystem system;
  ContextFacade ctx(&registry, 0.016f, nullptr, Internal::BulkGetComponent);

  for (auto _ : state) {
    const std::uint64_t before = counter.count;
    do {
      system(ctx);
    } while (counter.count == before);
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_CollisionDispatchDense)->Name("Sys/Collision/DenseSustainedPairs")->Range(16, 512)->UseRealTime();

namespace {
void BuildMixedShapesGrid(Registry& registry, const int n) {
  EntityMask mask;
  mask.set(0);
  const int cols = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(n))));
  for (int i = 0; i < n; ++i) {
    const Entity e = registry.CreateEntity();
    // 24px spacing with ~16-24px shapes produces neighbor overlaps across all 5 shape types.
    const int col = i % cols;
    const int row = i / cols;
    const float x = static_cast<float>(col) * 24.0f;
    const float y = static_cast<float>(row) * 24.0f;
    registry.AddComponent(e, GlobalTransformComponent{glm::vec2(x, y), glm::vec2(1.0f, 1.0f), 0.0f});
    registry.AddComponent(e, EntityMaskComponent(mask));
    switch (i % 5) {
      case 0:
        registry.AddComponent(e, BoxColliderComponent(28, 28, glm::vec2(0.0f, 0.0f), false, mask));
        break;
      case 1:
        registry.AddComponent(e, CircleColliderComponent(16.0f, glm::vec2(0.0f, 0.0f), false, mask));
        break;
      case 2:
        registry.AddComponent(
            e, CapsuleColliderComponent(glm::vec2(-8.0f, 0.0f), glm::vec2(8.0f, 0.0f), 10.0f, false, mask));
        break;
      case 3:
        registry.AddComponent(e, TriangleColliderComponent(glm::vec2(-14.0f, -14.0f), glm::vec2(18.0f, 0.0f),
                                                           glm::vec2(0.0f, 18.0f), false, mask));
        break;
      default:
        registry.AddComponent(e,
                              LineColliderComponent(glm::vec2(-16.0f, -16.0f), glm::vec2(16.0f, 16.0f), false, mask));
        break;
    }
  }
}
}  // namespace

static void BM_CollisionMixedShapesSync(benchmark::State& state) {
  Registry registry;
  EventBus bus;
  EngineContext ec;
  ec.eventBus = &bus;
  registry.Set<EngineContext>(ec);

  const int n = static_cast<int>(state.range(0));
  BuildMixedShapesGrid(registry, n);

  KDTreeCollisionSystem system(/*synchronous=*/true);
  ContextFacade ctx(&registry, 0.016f, nullptr, Internal::BulkGetComponent);
  system(ctx);  // warm up queries and initial pair set

  for (auto _ : state) {
    system(ctx);
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * n);
}
BENCHMARK(BM_CollisionMixedShapesSync)->Name("Sys/Collision/MixedShapesSync")->Range(64, 4096)->UseRealTime();
