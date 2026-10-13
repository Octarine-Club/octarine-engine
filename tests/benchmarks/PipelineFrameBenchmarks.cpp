#include <benchmark/benchmark.h>

#include <algorithm>
#include <functional>
#include <limits>
#include <string>

#include "BenchCommon.h"
#include "BenchWorld.h"
#include "Components/PositionComponent.h"
#include "Components/RigidBodyComponent.h"
#include "Components/SpriteComponent.h"
#include "Components/SquarePrimitiveComponent.h"
#include "ECS/Query.h"
#include "Systems/CombatSystem.h"
#include "Systems/EnemySteeringSystem.h"
#include "Systems/HomingSystem.h"
#include "Systems/KDTreeCollisionSystem.h"
#include "Systems/RenderPrimitiveSystem.h"
#include "Systems/RenderSpriteSystem.h"
#include "Systems/SeparationSystem.h"
#include "Systems/TransformSystem.h"
#include "Systems/VelocityIntegrationSystem.h"

// Pipeline/ tier: chains of systems through Registry::Update, reporting each system's share
// (sys_<Name>_us) so interaction effects (cache pressure, thread-pool contention, query upkeep)
// are visible next to the totals.
//
// Frame/ tier: the full headless simulation — engine_bootstrap::RegisterSimulationSystems (the
// exact Game::Setup set) + render-queue production + radix sort — on a dash-vs-shaped world,
// with per-frame latency percentiles and a max-entities-within-budget search.

using bench::BenchWorld;
using bench::WorldSpec;

namespace {

WorldSpec PipelineSpec(const int enemies) {
  WorldSpec spec;
  spec.enemies = enemies;
  spec.orbitWeapons = 8;
  spec.homingWeapons = 32;
  return spec;
}

void RunPipeline(benchmark::State& state, const WorldSpec& spec, const std::function<void(BenchWorld&)>& build,
                 const int warmup = 30) {
  BenchWorld world(spec);
  world.SpawnPopulation();
  build(world);
  for (int i = 0; i < warmup; ++i) world.Step();

  auto& registry = world.GetRegistry();
  bench::SystemTimings timings(registry);
  for (auto _ : state) {
    world.Step();
    timings.Sample();
  }
  bench::ReportRegistry(state, registry);
  bench::ReportPerEntity(state, spec.enemies);
  timings.Report(state);
}

struct MovementHandles {
  SystemHandle<VelocityIntegrationSystem> integrate;
};

MovementHandles RegisterMovement(Registry& registry, const bool withSeparation) {
  auto steering = registry.RegisterBulkSystem(EnemySteeringSystem());
  auto homing = registry.RegisterBulkSystem(HomingSystem());
  registry.Order(homing).After(steering);
  auto integrate = registry.RegisterParallelSystem<PositionComponent, RigidBodyComponent>(VelocityIntegrationSystem());
  if (withSeparation) {
    auto separation = registry.RegisterBulkSystem(SeparationSystem());
    registry.Order(separation).After(homing);
    registry.Order(integrate).After(separation);
  } else {
    registry.Order(integrate).After(homing);
  }
  return {integrate};
}

void RegisterPhysics(Registry& registry, const MovementHandles& movement) {
  auto transform = registry.RegisterBulkSystem<GlobalTransformComponent>(TransformSystem());
  auto collision = registry.RegisterBulkSystem(KDTreeCollisionSystem(/*synchronous=*/true));
  registry.Order(transform).After(movement.integrate);
  registry.Order(collision).After(transform);
}

}  // namespace

// steering -> homing -> separation -> integration -> transform. Capped at 25k: SeparationSystem is
// superlinear in horde density (see Sys/Separation) and dominates beyond that.
static void BM_PipelineMovement(benchmark::State& state) {
  RunPipeline(state, PipelineSpec(static_cast<int>(state.range(0))), [](BenchWorld& w) {
    w.RegisterDriver();
    auto& registry = w.GetRegistry();
    const auto movement = RegisterMovement(registry, true);
    auto transform = registry.RegisterBulkSystem<GlobalTransformComponent>(TransformSystem());
    registry.Order(transform).After(movement.integrate);
  });
}
BENCHMARK(BM_PipelineMovement)->Name("Pipeline/Movement")->Apply(bench::EntitySweepSmall)->UseRealTime();

// steering -> homing -> integration -> transform -> collision (sync, so detection is on the clock).
static void BM_PipelinePhysics(benchmark::State& state) {
  RunPipeline(state, PipelineSpec(static_cast<int>(state.range(0))), [](BenchWorld& w) {
    w.RegisterDriver();
    auto& registry = w.GetRegistry();
    RegisterPhysics(registry, RegisterMovement(registry, false));
  });
}
BENCHMARK(BM_PipelinePhysics)->Name("Pipeline/Physics")->Apply(bench::EntitySweep)->UseRealTime();

// Physics + CombatSystem with 8 player-parented weapon zones and optional retargeting homing
// weapons. Arg 1: 0 = immortal enemies (hits, no deaths), 1 = lethal (deaths + director
// respawns). Arg 2: homing weapons with retarget_on_hit (each hit rescans every HealthComponent).
static void BM_PipelineCombat(benchmark::State& state) {
  WorldSpec spec = PipelineSpec(static_cast<int>(state.range(0)));
  spec.lethal = state.range(1) != 0;
  spec.homingWeapons = static_cast<int>(state.range(2));
  state.SetLabel(spec.lethal ? "lethal" : "immortal");
  RunPipeline(state, spec, [](BenchWorld& w) {
    w.RegisterDriver();
    auto& registry = w.GetRegistry();
    RegisterPhysics(registry, RegisterMovement(registry, false));
    auto& combat = registry.Set<CombatSystem>(CombatSystem());
    combat.Init(&registry, w.GetBusPtr());
    registry.RegisterBulkSystem(std::ref(combat));
  });
}
BENCHMARK(BM_PipelineCombat)
    ->Name("Pipeline/Combat")
    ->ArgNames({"n", "lethal", "homing"})
    ->ArgsProduct({{1000, 5000, 10000, 25000}, {0, 1}, {0, 32}})
    ->UseRealTime();

// transform -> square + sprite render-queue emission (real camera) -> radix sort.
static void BM_PipelineRenderPrep(benchmark::State& state) {
  WorldSpec spec = PipelineSpec(static_cast<int>(state.range(0)));
  RunPipeline(state, spec, [](BenchWorld& w) {
    w.RegisterDriver();
    w.GetRegistry().RegisterBulkSystem<GlobalTransformComponent>(TransformSystem());
    w.RegisterRenderPrep();
  });
}
BENCHMARK(BM_PipelineRenderPrep)->Name("Pipeline/RenderPrep")->Apply(bench::EntitySweep)->UseRealTime();

// --- Frame tier ------------------------------------------------------------------------------

namespace {

constexpr int kFrameWarmup = 60;

// Frame-tier sweep. Stops well short of the ECS sweeps because whole-frame cost is dominated by
// superlinear systems (separation, collision); Frame/MaxEntities finds the actual ceiling.
void FrameSweep(benchmark::Benchmark* b) {
  for (const int n : {1000, 2500, 5000, 10000, 20000}) b->Arg(n);
}

// A wave-shaped dash-vs fight: mixed behaviours, some spitters and trail droppers feeding the
// projectile/hazard pools, a gem field, orbiting weapons and homing weapons.
WorldSpec FrameSpec(const int enemies, const bool lethal) {
  WorldSpec spec;
  spec.enemies = enemies;
  spec.spitterFraction = 0.05f;
  spec.trailFraction = 0.02f;
  spec.gems = std::min(enemies / 10, 5000);
  spec.orbitWeapons = 6;
  spec.homingWeapons = 16;
  spec.lethal = lethal;
  return spec;
}

constexpr const char* kEnemyScript = R"lua(
  return function()
    return {
      t = 0,
      on_update = function(self, entity, dt)
        self.t = self.t + dt
        local rb = registry.get_rigidbody(entity)
        if rb and self.t > 1000 then rb.velocity.x = 0 end
      end,
    }
  end
)lua";

struct FrameOptions {
  bool synchronousCollision = false;
  int luaStride = 0;  // 0 = no Lua scripts
};

std::unique_ptr<BenchWorld> BuildFrameWorld(const WorldSpec& spec, const FrameOptions& options) {
  auto world = std::make_unique<BenchWorld>(spec);
  world->SpawnPopulation();
  // Game::Setup registers ScriptSystem before everything else; mirror that.
  if (options.luaStride > 0) world->AttachLuaScripts(kEnemyScript, options.luaStride);
  world->RegisterDriver();
  world->RegisterSimulation(options.synchronousCollision);
  world->RegisterRenderPrep();
  return world;
}

void RunFrame(benchmark::State& state, const WorldSpec& spec, const FrameOptions& options) {
  auto world = BuildFrameWorld(spec, options);
  for (int i = 0; i < kFrameWarmup; ++i) world->Step();

  auto& registry = world->GetRegistry();
  bench::SystemTimings timings(registry);
  bench::FrameStats stats;
  for (auto _ : state) {
    const auto start = bench::Clock::now();
    world->Step();
    const double ns = bench::ElapsedNs(start);
    state.SetIterationTime(ns / 1.0e9);
    stats.Add(ns);
    timings.Sample();
  }
  bench::ReportRegistry(state, registry);
  bench::ReportPerEntity(state, spec.enemies);
  stats.Report(state);
  timings.Report(state);
}

}  // namespace

// The shipped configuration: async collision (detection overlaps later frames on the pool).
static void BM_FrameSim(benchmark::State& state) {
  RunFrame(state, FrameSpec(static_cast<int>(state.range(0)), false), {});
}
BENCHMARK(BM_FrameSim)->Name("Frame/Sim")->Apply(FrameSweep)->UseManualTime()->Unit(benchmark::kMillisecond);

// Synchronous collision: the whole detection cost lands on the frame thread. Deterministic and
// the conservative view of CPU cost per frame.
static void BM_FrameSimSync(benchmark::State& state) {
  RunFrame(state, FrameSpec(static_cast<int>(state.range(0)), false), {.synchronousCollision = true});
}
BENCHMARK(BM_FrameSimSync)->Name("Frame/SimSync")->Apply(FrameSweep)->UseManualTime()->Unit(benchmark::kMillisecond);

// Lethal weapons + perimeter respawns: sustained death/spawn churn at a fixed population.
static void BM_FrameChurn(benchmark::State& state) {
  RunFrame(state, FrameSpec(static_cast<int>(state.range(0)), true), {.synchronousCollision = true});
}
BENCHMARK(BM_FrameChurn)->Name("Frame/Churn")->Apply(FrameSweep)->UseManualTime()->Unit(benchmark::kMillisecond);

// Every enemy carries a Lua ScriptComponent with an on_update touching a component proxy, as
// dash-vs enemies carry script tables.
static void BM_FrameLua(benchmark::State& state) {
  RunFrame(state, FrameSpec(static_cast<int>(state.range(0)), false), {.synchronousCollision = true, .luaStride = 1});
}
BENCHMARK(BM_FrameLua)->Name("Frame/Lua")->Apply(FrameSweep)->UseManualTime()->Unit(benchmark::kMillisecond);

namespace {

// p95 frame time at `enemies`, or DBL_MAX as soon as any warmup frame blows 4x the budget (deep in
// the superlinear zone a full probe could take minutes and its exact value is irrelevant).
double ProbeP95Ns(const int enemies, const double budgetNs) {
  auto world = BuildFrameWorld(FrameSpec(enemies, false), {.synchronousCollision = true});
  for (int i = 0; i < 60; ++i) {
    const auto start = bench::Clock::now();
    world->Step();
    if (bench::ElapsedNs(start) > 4.0 * budgetNs) return std::numeric_limits<double>::max();
  }
  std::vector<double> samples;
  constexpr int kFrames = 120;
  samples.reserve(kFrames);
  for (int i = 0; i < kFrames; ++i) {
    const auto start = bench::Clock::now();
    world->Step();
    samples.push_back(bench::ElapsedNs(start));
  }
  std::sort(samples.begin(), samples.end());
  return samples[static_cast<size_t>(0.95 * (kFrames - 1))];
}

}  // namespace

// Headline capacity number: the largest enemy count whose p95 Frame/SimSync time fits the budget
// (arg: target Hz). Exponential search then bisection to within 5%.
static void BM_MaxEntities(benchmark::State& state) {
  const double budgetNs = state.range(0) == 120 ? bench::kBudget120Ns : bench::kBudget60Ns;
  int best = 0;
  double bestP95 = 0.0;
  int probes = 0;
  for (auto _ : state) {
    const auto start = bench::Clock::now();
    int lo = 0;
    int hi = 2000;
    constexpr int kCeiling = 409600;
    double p95 = 0.0;
    while (hi <= kCeiling && (p95 = ProbeP95Ns(hi, budgetNs), ++probes, p95 <= budgetNs)) {
      lo = hi;
      bestP95 = p95;
      hi *= 2;
    }
    while (hi - lo > std::max(500, lo / 20)) {
      const int mid = lo + (hi - lo) / 2;
      p95 = ProbeP95Ns(mid, budgetNs);
      ++probes;
      if (p95 <= budgetNs) {
        lo = mid;
        bestP95 = p95;
      } else {
        hi = mid;
      }
    }
    best = lo;
    state.SetIterationTime(bench::ElapsedNs(start) / 1.0e9);
  }
  state.counters["max_entities"] = best;
  state.counters["p95_at_max_ms"] = bestP95 / 1.0e6;
  state.counters["probes"] = probes;
}
BENCHMARK(BM_MaxEntities)
    ->Name("Frame/MaxEntities")
    ->ArgName("hz")
    ->Arg(60)
    ->Arg(120)
    ->Iterations(1)
    ->UseManualTime()
    ->Unit(benchmark::kSecond);
