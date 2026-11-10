#include <benchmark/benchmark.h>

#include <sol/sol.hpp>
#include <vector>

#include "BenchCommon.h"
#include "Components/AnimationComponent.h"
#include "Components/BoxColliderComponent.h"
#include "Components/CameraComponents.h"
#include "Components/EntityMaskComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/LifetimeComponent.h"
#include "Components/PositionComponent.h"
#include "Components/RigidBodyComponent.h"
#include "Components/ScriptComponent.h"
#include "Components/SpriteComponent.h"
#include "ECS/EntityPool.h"
#include "Engine/EngineBootstrap.h"
#include "Engine/SimulationSystems.h"
#include "EventBus/EventBus.h"
#include "Game/EngineContext.h"
#include "Game/GameConfig.h"
#include "Renderer/RenderQueue.h"
#include "Systems/AnimationSystem.h"
#include "Systems/KDTreeCollisionSystem.h"
#include "Systems/LifetimeSystem.h"
#include "Systems/RenderPrimitiveSystem.h"
#include "Systems/RenderSpriteSystem.h"
#include "Systems/ScriptSystem.h"
#include "Systems/TransformSystem.h"
#include "Systems/VelocityIntegrationSystem.h"

namespace {

void SetupCoreContext(Registry& registry, EventBus& bus, sol::state* lua = nullptr) {
  auto& cfg = registry.Set<GameConfig>(GameConfig());
  cfg.windowWidth = 1920;
  cfg.windowHeight = 1080;
  cfg.playableAreaWidth = 4000.0f;
  cfg.playableAreaHeight = 4000.0f;

  EngineContext ec{};
  ec.eventBus = &bus;
  ec.config = &cfg;
  auto& storedCtx = registry.Set<EngineContext>(ec);

  engine_bootstrap::InstallCoreSingletons(registry, storedCtx, 1920, 1080, /*withFramePathCaches=*/true, lua);
  engine_bootstrap::InstallPoolAndSingletons(registry);
}

void PopulateCoreEntities(Registry& registry, const int n) {
  EntityMask mask{};
  mask.set(0);

  for (int i = 0; i < n; ++i) {
    const auto x = 100.0f + static_cast<float>((i * 31) % 3600);
    const auto y = 100.0f + static_cast<float>((i * 47) % 3600);
    registry.CreateEntityWithBundle(PositionComponent{glm::vec2(x, y)}, RigidBodyComponent{glm::vec2(10.0f, 5.0f)},
                                    GlobalTransformComponent{glm::vec2(x, y), glm::vec2(1.0f, 1.0f), 0.0f},
                                    BoxColliderComponent(32, 32, glm::vec2(0.0f, 0.0f), false, mask),
                                    EntityMaskComponent(mask), SpriteComponent("dummy_tex", 32.0f, 32.0f),
                                    AnimationComponent(4, 10, true));
  }
}

// -----------------------------------------------------------------------------
// Pipeline Tiers
// -----------------------------------------------------------------------------

void BM_Pipeline_CoreMovement(benchmark::State& state) {
  Registry registry;
  EventBus bus;
  SetupCoreContext(registry, bus);

  registry.RegisterParallelSystem<SpriteComponent, AnimationComponent>(AnimationSystem());
  auto vel = registry.RegisterParallelSystem<PositionComponent, RigidBodyComponent>(VelocityIntegrationSystem());
  auto trans = registry.RegisterBulkSystem<GlobalTransformComponent>(TransformSystem());
  registry.Order(trans).After(vel);

  const auto n = static_cast<int>(state.range(0));
  PopulateCoreEntities(registry, n);

  bench::SystemTimings timings(registry);
  // Warm-up step
  registry.Update(bench::kDt);
  timings.Sample();

  bench::AllocScope allocScope;
  bench::HwCounterScope hwScope(static_cast<double>(n));

  for (auto _ : state) {
    registry.Update(bench::kDt);
    timings.Sample();
  }

  allocScope.Stop();
  hwScope.Stop();

  bench::ReportPerEntity(state, static_cast<double>(n));
  bench::ReportRegistry(state, registry);
  timings.Report(state);
  allocScope.Report(state);
  hwScope.Report(state);
}
BENCHMARK(BM_Pipeline_CoreMovement)->Name("Pipeline/CoreMovement")->UseRealTime()->Apply(bench::EntitySweepSmall);

void BM_Pipeline_CoreCollision(benchmark::State& state) {
  Registry registry;
  EventBus bus;
  SetupCoreContext(registry, bus);

  auto vel = registry.RegisterParallelSystem<PositionComponent, RigidBodyComponent>(VelocityIntegrationSystem());
  auto trans = registry.RegisterBulkSystem<GlobalTransformComponent>(TransformSystem());
  auto col = registry.RegisterBulkSystem(KDTreeCollisionSystem(true));
  registry.Order(trans).After(vel);
  registry.Order(col).After(trans);

  const auto n = static_cast<int>(state.range(0));
  PopulateCoreEntities(registry, n);

  bench::SystemTimings timings(registry);
  // Warm-up step
  registry.Update(bench::kDt);
  timings.Sample();

  bench::AllocScope allocScope;
  bench::HwCounterScope hwScope(static_cast<double>(n));

  for (auto _ : state) {
    registry.Update(bench::kDt);
    timings.Sample();
  }

  allocScope.Stop();
  hwScope.Stop();

  bench::ReportPerEntity(state, static_cast<double>(n));
  bench::ReportRegistry(state, registry);
  timings.Report(state);
  allocScope.Report(state);
  hwScope.Report(state);
}
BENCHMARK(BM_Pipeline_CoreCollision)->Name("Pipeline/CoreCollision")->UseRealTime()->Apply(bench::EntitySweepSmall);

// -----------------------------------------------------------------------------
// Frame Tiers
// -----------------------------------------------------------------------------

void BM_Frame_CoreSim(benchmark::State& state) {
  Registry registry;
  auto bus = std::make_unique<EventBus>();
  SetupCoreContext(registry, *bus);

  engine_bootstrap::RegisterSimulationSystems(registry, bus);

  const auto n = static_cast<int>(state.range(0));
  PopulateCoreEntities(registry, n);

  bench::SystemTimings timings(registry);
  // Warm-up step
  auto& rq = registry.Get<RenderQueue>();
  rq.Clear();
  registry.Update(bench::kDt);
  rq.Sort();
  timings.Sample();

  bench::FrameStats frameStats;
  bench::AllocScope allocScope;
  bench::HwCounterScope hwScope(static_cast<double>(n));

  for (auto _ : state) {
    rq.Clear();
    const auto start = bench::Clock::now();

    registry.Update(bench::kDt);
    rq.Sort();

    const double elapsedNs = bench::ElapsedNs(start);
    state.SetIterationTime(elapsedNs / 1.0e9);
    frameStats.Add(elapsedNs);
    timings.Sample();
  }

  allocScope.Stop();
  hwScope.Stop();

  bench::ReportPerEntity(state, static_cast<double>(n));
  bench::ReportRegistry(state, registry);
  frameStats.Report(state);
  timings.Report(state);
  allocScope.Report(state);
  hwScope.Report(state);
}
BENCHMARK(BM_Frame_CoreSim)->Name("Frame/CoreSim")->UseManualTime()->Apply(bench::EntitySweepSmall);

void BM_Frame_CoreLua(benchmark::State& state) {
  Registry registry;
  auto bus = std::make_unique<EventBus>();

  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::math);
  SetupCoreContext(registry, *bus, &lua);

  ScriptSystem scriptSystem;
  scriptSystem.CreateLuaBindings(lua);

  lua.script(R"(
    function on_tick(self, entity, dt)
      local x = math.sin(dt) * 0.1
    end
  )");

  registry.RegisterSystem<ScriptComponent>(scriptSystem);
  engine_bootstrap::RegisterSimulationSystems(registry, bus);

  const auto n = static_cast<int>(state.range(0));
  PopulateCoreEntities(registry, n);

  sol::function luaFn = lua["on_tick"];
  auto query = registry.CreateQuery<PositionComponent>();
  query->Update();
  std::vector<Entity> entities;
  entities.reserve(static_cast<size_t>(n));
  query->ForEach([&](const Entity e, PositionComponent&) { entities.push_back(e); });
  for (const Entity e : entities) {
    ScriptComponent sc{};
    sc.updateFunction = luaFn;
    registry.AddComponent(e, sc);
  }

  bench::SystemTimings timings(registry);
  // Warm-up step
  registry.Update(bench::kDt);
  timings.Sample();

  bench::FrameStats frameStats;
  bench::AllocScope allocScope;
  bench::LuaMemoryScope luaScope(lua.lua_state());
  bench::HwCounterScope hwScope(static_cast<double>(n));

  for (auto _ : state) {
    const auto start = bench::Clock::now();
    registry.Update(bench::kDt);
    const double elapsedNs = bench::ElapsedNs(start);
    state.SetIterationTime(elapsedNs / 1.0e9);
    frameStats.Add(elapsedNs);
    timings.Sample();
  }

  allocScope.Stop();
  luaScope.Stop();
  hwScope.Stop();

  bench::ReportPerEntity(state, static_cast<double>(n));
  bench::ReportRegistry(state, registry);
  frameStats.Report(state);
  timings.Report(state);
  allocScope.Report(state);
  luaScope.Report(state);
  hwScope.Report(state);
}
BENCHMARK(BM_Frame_CoreLua)->Name("Frame/CoreLua")->UseManualTime()->Apply(bench::EntitySweepSmall);

}  // namespace
