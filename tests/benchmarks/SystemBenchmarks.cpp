#include <benchmark/benchmark.h>

#include <functional>
#include <random>
#include <string>

#include "BenchCommon.h"
#include "BenchWorld.h"
#include "Components/AnimationComponent.h"
#include "Components/CameraComponents.h"
#include "Components/LifetimeComponent.h"
#include "Components/PositionComponent.h"
#include "Components/ProjectileComponent.h"
#include "Components/RigidBodyComponent.h"
#include "Components/SpriteComponent.h"
#include "ECS/Query.h"
#include "Events/CombatHitEvent.h"
#include "Game/GameConfig.h"
#include "Renderer/RenderQueue.h"
#include "Systems/AnimationSystem.h"
#include "Systems/CullDistanceSystem.h"
#include "Systems/EnemySteeringSystem.h"
#include "Systems/HomingSystem.h"
#include "Systems/KDTreeCollisionSystem.h"
#include "Systems/LifetimeSystem.h"
#include "Systems/MagnetSystem.h"
#include "Systems/OffScreenDespawnSystem.h"
#include "Systems/ProjectileEmitSystem.h"
#include "Systems/ProjectileLifecycleSystem.h"
#include "Systems/RenderPrimitiveSystem.h"
#include "Systems/RenderSpriteSystem.h"
#include "Systems/SeparationSystem.h"
#include "Systems/TransformSystem.h"
#include "Systems/VelocityIntegrationSystem.h"

// Sys/ tier: one engine system at a time on a dash-vs-shaped population (BenchWorld), timed through
// Registry::Update so query maintenance is included exactly as in the game.

using bench::BenchWorld;
using bench::Layout;
using bench::WorldSpec;

namespace {

constexpr int kWarmupFrames = 3;

const char* LayoutName(const Layout layout) {
  switch (layout) {
    case Layout::Horde:
      return "Horde";
    case Layout::Uniform:
      return "Uniform";
    case Layout::Cluster:
      return "Cluster";
  }
  return "?";
}

// Builds a world, lets `registerSystems` add the system(s) under test, warms up, then times
// Registry::Update. `entitiesPerFrame` normalises ns_per_entity (defaults to spec.enemies).
void RunSystem(benchmark::State& state, const WorldSpec& spec, const std::function<void(BenchWorld&)>& registerSystems,
               double entitiesPerFrame = -1.0, const float dt = bench::kDt) {
  BenchWorld world(spec);
  world.SpawnPopulation();
  registerSystems(world);
  auto& registry = world.GetRegistry();
  for (int i = 0; i < kWarmupFrames; ++i) registry.Update(dt);

  bench::SystemTimings timings(registry);
  for (auto _ : state) {
    registry.Update(dt);
    timings.Sample();
  }
  bench::ReportRegistry(state, registry);
  bench::ReportPerEntity(state, entitiesPerFrame > 0.0 ? entitiesPerFrame : spec.enemies);
  timings.Report(state);
}

WorldSpec Spec(const int enemies, const Layout layout = Layout::Horde) {
  WorldSpec spec;
  spec.enemies = enemies;
  spec.layout = layout;
  return spec;
}

}  // namespace

// --- Movement systems ----------------------------------------------------------------------------

static void BM_EnemySteering(benchmark::State& state) {
  RunSystem(state, Spec(static_cast<int>(state.range(0))),
            [](BenchWorld& w) { w.GetRegistry().RegisterBulkSystem(EnemySteeringSystem()); });
}
BENCHMARK(BM_EnemySteering)->Name("Sys/EnemySteering")->Apply(bench::EntitySweep)->UseRealTime();

// Separation's spatial grid is a fixed 128x128 lattice of 64 px cells centred on the origin, and
// per-cell work is quadratic in occupancy: Horde shows density growth past the cull radius,
// Uniform shows what happens once the population spills past the grid edge, Cluster is the
// dense-knot worst case.
static void BM_Separation(benchmark::State& state) {
  const auto layout = static_cast<Layout>(state.range(1));
  state.SetLabel(LayoutName(layout));
  RunSystem(state, Spec(static_cast<int>(state.range(0)), layout),
            [](BenchWorld& w) { w.GetRegistry().RegisterBulkSystem(SeparationSystem()); });
}
BENCHMARK(BM_Separation)
    ->Name("Sys/Separation")
    ->ArgNames({"n", "layout"})
    ->ArgsProduct({{1000, 5000, 10000, 25000, 50000}, {0, 1, 2}})
    ->UseRealTime();

static void BM_Homing(benchmark::State& state) {
  WorldSpec spec = Spec(1000);
  spec.homingWeapons = static_cast<int>(state.range(0));
  RunSystem(
      state, spec, [](BenchWorld& w) { w.GetRegistry().RegisterBulkSystem(HomingSystem()); },
      static_cast<double>(spec.homingWeapons));
}
BENCHMARK(BM_Homing)->Name("Sys/Homing")->Arg(100)->Arg(1000)->Arg(10000)->UseRealTime();

static void BM_Magnet(benchmark::State& state) {
  WorldSpec spec = Spec(0);
  spec.gems = static_cast<int>(state.range(0));
  RunSystem(
      state, spec, [](BenchWorld& w) { w.GetRegistry().RegisterBulkSystem(MagnetSystem()); },
      static_cast<double>(spec.gems));
}
BENCHMARK(BM_Magnet)->Name("Sys/Magnet")->Arg(1000)->Arg(10000)->Arg(50000)->UseRealTime();

// CullDistanceSystem only does work every 0.5 s; a 0.5 s dt forces the full pass every Update so
// the per-pass cost is measured (divide by 30 for the amortised per-frame cost at 60 Hz).
static void BM_CullDistance(benchmark::State& state) {
  RunSystem(
      state, Spec(static_cast<int>(state.range(0))),
      [](BenchWorld& w) { w.GetRegistry().RegisterBulkSystem(CullDistanceSystem()); }, -1.0, 0.5f);
}
BENCHMARK(BM_CullDistance)->Name("Sys/CullDistance/FullPass")->Apply(bench::EntitySweep)->UseRealTime();

static void BM_VelocityIntegration(benchmark::State& state) {
  RunSystem(state, Spec(static_cast<int>(state.range(0))), [](BenchWorld& w) {
    w.GetRegistry().RegisterParallelSystem<PositionComponent, RigidBodyComponent>(VelocityIntegrationSystem());
  });
}
BENCHMARK(BM_VelocityIntegration)->Name("Sys/VelocityIntegration")->Apply(bench::EntitySweep)->UseRealTime();

// Arg 1: player-parented orbit weapons. Any ChildOf pair routes part of the frame through the
// hierarchical path; this shows whether that taxes the flat majority.
static void BM_Transform(benchmark::State& state) {
  WorldSpec spec = Spec(static_cast<int>(state.range(0)));
  spec.orbitWeapons = static_cast<int>(state.range(1));
  RunSystem(state, spec,
            [](BenchWorld& w) { w.GetRegistry().RegisterBulkSystem<GlobalTransformComponent>(TransformSystem()); });
}
BENCHMARK(BM_Transform)
    ->Name("Sys/Transform")
    ->ArgNames({"n", "child_weapons"})
    ->ArgsProduct({{1000, 10000, 50000, 100000}, {0, 8}})
    ->UseRealTime();

// --- Collision -----------------------------------------------------------------------------------

namespace {

void RegisterCollisionStack(BenchWorld& w, const bool moving, const bool synchronous) {
  auto& registry = w.GetRegistry();
  if (moving) {
    w.RegisterDriver();
    auto steering = registry.RegisterBulkSystem(EnemySteeringSystem());
    auto integrate =
        registry.RegisterParallelSystem<PositionComponent, RigidBodyComponent>(VelocityIntegrationSystem());
    registry.Order(integrate).After(steering);
  }
  auto transform = registry.RegisterBulkSystem<GlobalTransformComponent>(TransformSystem());
  auto collision = registry.RegisterBulkSystem(KDTreeCollisionSystem(synchronous));
  registry.Order(collision).After(transform);
}

}  // namespace

// Synchronous detection on the frame thread: the full median-cut broadphase + narrowphase +
// enter/exit bookkeeping cost per frame. Static = no motion (pair set stable after frame 1);
// Moving = enemies chase an orbiting player, so pairs enter/exit every frame.
// Args: {n, layout, moving}. sys_KDTreeCollisionSystem_us isolates detection from the movers.
static void BM_CollisionSync(benchmark::State& state) {
  const auto layout = static_cast<Layout>(state.range(1));
  const bool moving = state.range(2) != 0;
  state.SetLabel(std::string(LayoutName(layout)) + (moving ? "/moving" : "/static"));
  WorldSpec spec = Spec(static_cast<int>(state.range(0)), layout);
  spec.orbitWeapons = 8;
  RunSystem(state, spec, [moving](BenchWorld& w) { RegisterCollisionStack(w, moving, true); });
}
BENCHMARK(BM_CollisionSync)
    ->Name("Sys/Collision/Sync")
    ->ArgNames({"n", "layout", "moving"})
    ->ArgsProduct({{1000, 5000, 10000, 25000, 50000}, {0, 1, 2}, {0, 1}})
    ->UseRealTime();

// Async (game default): the frame thread only dispatches/collects; detection overlaps later
// frames on the ThreadPool. Measures the frame-thread cost — detection latency is hidden.
static void BM_CollisionAsync(benchmark::State& state) {
  WorldSpec spec = Spec(static_cast<int>(state.range(0)));
  spec.orbitWeapons = 8;
  RunSystem(state, spec, [](BenchWorld& w) { RegisterCollisionStack(w, true, false); });
}
BENCHMARK(BM_CollisionAsync)->Name("Sys/Collision/Async")->Apply(bench::EntitySweepSmall)->UseRealTime();

// --- Projectile / lifetime churn ---------------------------------------------------------------

// Every enemy is a spitter (fires every 2 s, 4 s projectile life) so the projectile pool reaches
// a steady ~2N live projectiles with N/120 spawns and despawns per frame. Measures pool
// spawn/park through ProjectileEmit + ProjectileLifecycle's CommandBuffer + the despawn flush.
static void BM_ProjectileChurn(benchmark::State& state) {
  WorldSpec spec = Spec(static_cast<int>(state.range(0)));
  spec.spitterFraction = 1.0f;
  BenchWorld world(spec);
  world.SpawnPopulation();
  auto& registry = world.GetRegistry();
  registry.RegisterBulkSystem(registry.Get<ProjectileEmitSystem>());
  registry.RegisterParallelSystem<ProjectileComponent>(ProjectileLifecycleSystem());
  registry.RegisterBulkSystem<LifetimeComponent>(LifetimeSystem());
  for (int i = 0; i < 300; ++i) registry.Update(bench::kDt);  // reach steady state (> 4 s)

  bench::SystemTimings timings(registry);
  for (auto _ : state) {
    registry.Update(bench::kDt);
    timings.Sample();
  }
  bench::ReportRegistry(state, registry);
  bench::ReportPerEntity(state, static_cast<double>(registry.GetUserEntityCount()));
  timings.Report(state);
}
BENCHMARK(BM_ProjectileChurn)->Name("Sys/ProjectileChurn")->Arg(1000)->Arg(5000)->Arg(20000)->UseRealTime();

namespace {

// Bare sprite population (projectile-like: Position + GlobalTransform + Sprite) for the
// sprite-path systems; spans the 4000 px arena so most are off-camera, as in play.
void SpawnSprites(BenchWorld& w, const int count, const bool withAnimation, const bool withLifetime) {
  auto& registry = w.GetRegistry();
  for (int i = 0; i < count; ++i) {
    const glm::vec2 p = w.GetCenter() + glm::vec2(w.RandomFloat(-1900.0f, 1900.0f), w.RandomFloat(-1900.0f, 1900.0f));
    const Entity e = registry.CreateEntityWithBundle(PositionComponent(p), GlobalTransformComponent{p, {1, 1}, 0.0f},
                                                     SpriteComponent("", 8.0f, 8.0f, 4));
    if (withAnimation) registry.AddComponent(e, AnimationComponent(8, 12, true));
    if (withLifetime) registry.AddComponent(e, LifetimeComponent(1.0e6f));
  }
}

}  // namespace

static void BM_Lifetime(benchmark::State& state) {
  const auto n = static_cast<int>(state.range(0));
  RunSystem(
      state, Spec(0),
      [n](BenchWorld& w) {
        SpawnSprites(w, n, false, true);
        w.GetRegistry().RegisterBulkSystem<LifetimeComponent>(LifetimeSystem());
      },
      n);
}
BENCHMARK(BM_Lifetime)->Name("Sys/Lifetime")->Apply(bench::EntitySweep)->UseRealTime();

static void BM_OffScreenDespawn(benchmark::State& state) {
  const auto n = static_cast<int>(state.range(0));
  RunSystem(
      state, Spec(0),
      [n](BenchWorld& w) {
        SpawnSprites(w, n, false, false);
        w.GetRegistry().RegisterParallelSystem<PositionComponent, SpriteComponent>(OffScreenDespawnSystem());
      },
      n);
}
BENCHMARK(BM_OffScreenDespawn)->Name("Sys/OffScreenDespawn")->Apply(bench::EntitySweep)->UseRealTime();

static void BM_Animation(benchmark::State& state) {
  const auto n = static_cast<int>(state.range(0));
  RunSystem(
      state, Spec(0),
      [n](BenchWorld& w) {
        SpawnSprites(w, n, true, false);
        w.GetRegistry().RegisterParallelSystem<SpriteComponent, AnimationComponent>(AnimationSystem());
      },
      n);
}
BENCHMARK(BM_Animation)->Name("Sys/Animation")->Apply(bench::EntitySweep)->UseRealTime();

// --- Render preparation ----------------------------------------------------------------------

namespace {

// Expands camera + logical window to the whole arena so every renderable is emitted.
void MakeEverythingVisible(BenchWorld& w) {
  auto& registry = w.GetRegistry();
  auto& config = registry.Get<GameConfig>();
  config.windowWidth = static_cast<int>(config.playableAreaWidth);
  config.windowHeight = static_cast<int>(config.playableAreaHeight);
  registry.Get<CameraComponent>().viewport = {0.0f, 0.0f, config.playableAreaWidth, config.playableAreaHeight};
}

void RunRender(benchmark::State& state, BenchWorld& world, const double entities) {
  auto& registry = world.GetRegistry();
  for (int i = 0; i < kWarmupFrames; ++i) world.Step();
  auto& queue = registry.Get<RenderQueue>();
  bench::SystemTimings timings(registry);
  double emitted = 0.0;
  double sortNs = 0.0;
  for (auto _ : state) {
    world.BeginFrame();
    registry.Update(bench::kDt);
    timings.Sample();
    emitted += static_cast<double>(queue.Size());
    const auto sortStart = bench::Clock::now();
    world.EndFrame();
    sortNs += bench::ElapsedNs(sortStart);
  }
  const auto iters = static_cast<double>(state.iterations());
  state.counters["emitted"] = emitted / iters;
  state.counters["sort_us"] = sortNs / iters / 1000.0;
  bench::ReportRegistry(state, registry);
  bench::ReportPerEntity(state, entities);
  timings.Report(state);
}

}  // namespace

// Squares are dash-vs's main renderable. Arg 1: 0 = real 1280x720 camera (most culled),
// 1 = everything visible (emission + sort of the whole population).
static void BM_RenderPrimitive(benchmark::State& state) {
  const bool allVisible = state.range(1) != 0;
  state.SetLabel(allVisible ? "all_visible" : "camera_culled");
  BenchWorld world(Spec(static_cast<int>(state.range(0))));
  world.SpawnPopulation();
  auto& registry = world.GetRegistry();
  if (allVisible) {
    MakeEverythingVisible(world);
  } else {
    world.RegisterDriver();  // camera follows the player
  }
  // Squares are positioned from GlobalTransformComponent; resolve it (reported separately as
  // sys_TransformSystem_us) so culling sees real positions.
  auto transform = registry.RegisterBulkSystem<GlobalTransformComponent>(TransformSystem());
  auto render =
      registry.RegisterParallelSystem<SquarePrimitiveComponent, GlobalTransformComponent>(RenderPrimitiveSystem());
  registry.Order(render).After(transform);
  RunRender(state, world, static_cast<double>(state.range(0)));
}
BENCHMARK(BM_RenderPrimitive)
    ->Name("Sys/RenderPrimitive")
    ->ArgNames({"n", "all_visible"})
    ->ArgsProduct({{1000, 10000, 50000, 100000}, {0, 1}})
    ->UseRealTime();

// Sprite path: RenderSpriteSystem::Prepare walks every sprite serially to warm SpriteRenderCache
// before the parallel emit; this measures both halves.
static void BM_RenderSprite(benchmark::State& state) {
  const bool allVisible = state.range(1) != 0;
  state.SetLabel(allVisible ? "all_visible" : "camera_culled");
  const auto n = static_cast<int>(state.range(0));
  BenchWorld world(Spec(0));
  SpawnSprites(world, n, false, false);
  if (allVisible) MakeEverythingVisible(world);
  world.GetRegistry().RegisterParallelSystem<GlobalTransformComponent, SpriteComponent>(RenderSpriteSystem());
  RunRender(state, world, n);
}
BENCHMARK(BM_RenderSprite)
    ->Name("Sys/RenderSprite")
    ->ArgNames({"n", "all_visible"})
    ->ArgsProduct({{1000, 10000, 50000, 100000}, {0, 1}})
    ->UseRealTime();

// RenderQueue radix sort alone on N square commands with random layers/depths.
static void BM_RenderQueueSort(benchmark::State& state) {
  RenderQueue queue;
  const auto n = static_cast<size_t>(state.range(0));
  // NOLINTNEXTLINE(cert-msc51-cpp) — fixed seed keeps the key distribution identical across runs
  std::mt19937 rng(7);
  std::uniform_int_distribution<unsigned int> layer(0, 7);
  std::uniform_real_distribution<float> depth(0.0f, 4000.0f);
  for (auto _ : state) {
    state.PauseTiming();
    queue.Clear();
    for (size_t i = 0; i < n; ++i) queue.EmplaceSquare(layer(rng), depth(rng));
    state.ResumeTiming();
    queue.Sort();
  }
  bench::ReportPerEntity(state, static_cast<double>(n));
}
BENCHMARK(BM_RenderQueueSort)->Name("Sys/RenderQueueSort")->RangeMultiplier(4)->Range(1024, 262144);

// --- EventBus ------------------------------------------------------------------------------------

namespace {
struct HitSink {
  int64_t hits = 0;
  void OnHit(const CombatHitEvent& /*e*/) { ++hits; }
};
}  // namespace

// Emit + synchronous dispatch of N small events to S subscribers (CombatHitEvent: what every
// weapon hit produces). Args: {events, subscribers}.
static void BM_EventBusDispatch(benchmark::State& state) {
  EventBus bus;
  std::vector<HitSink> sinks(static_cast<size_t>(state.range(1)));
  std::vector<EventBus::SubscriptionHandle> subs;
  subs.reserve(sinks.size());
  for (auto& sink : sinks) subs.push_back(bus.SubscribeEvent<HitSink, CombatHitEvent>(&sink, &HitSink::OnHit));
  const auto events = state.range(0);
  for (auto _ : state) {
    for (int64_t i = 0; i < events; ++i) bus.EmitEvent<CombatHitEvent>(Entity{}, Entity{});
  }
  benchmark::DoNotOptimize(sinks.front().hits);
  bench::ReportPerEntity(state, static_cast<double>(events));
}
BENCHMARK(BM_EventBusDispatch)
    ->Name("Sys/EventBus/Dispatch")
    ->ArgNames({"events", "subscribers"})
    ->ArgsProduct({{100, 10000}, {1, 4}});

// --- Lua scripting -----------------------------------------------------------------------------

// Per-entity sol2 round trip through ScriptSystem with a dash-vs-style on_update that reads and
// writes a component via the registry.get_* zero-copy proxy.
static void BM_LuaScriptUpdate(benchmark::State& state) {
  const auto n = static_cast<int>(state.range(0));
  RunSystem(state, Spec(n), [](BenchWorld& w) {
    w.AttachLuaScripts(R"lua(
      return function()
        return {
          t = 0,
          on_update = function(self, entity, dt)
            self.t = self.t + dt
            local rb = registry.get_rigidbody(entity)
            if rb then rb.velocity.x = rb.velocity.x * 0.5 end
          end,
        }
      end
    )lua");
  });
}
BENCHMARK(BM_LuaScriptUpdate)->Name("Sys/Lua/ScriptUpdate")->Arg(1000)->Arg(5000)->Arg(20000)->UseRealTime();
