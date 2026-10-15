#include "BenchWorld.h"

#ifdef OCTARINE_HAS_GAME_MODULE

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "BenchCommon.h"
#include "Components/BoxColliderComponent.h"
#include "Components/CameraComponents.h"
#include "Components/EnemySteeringComponent.h"
#include "Components/EntityMaskComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/HealthComponent.h"
#include "Components/HomingComponent.h"
#include "Components/MagnetComponent.h"
#include "Components/PositionComponent.h"
#include "Components/ProjectileEmitterComponent.h"
#include "Components/RigidBodyComponent.h"
#include "Components/ScriptComponent.h"
#include "Components/SeparationComponent.h"
#include "Components/SquarePrimitiveComponent.h"
#include "Components/WeaponZoneComponent.h"
#include "ECS/Query.h"
#include "Engine/EngineBootstrap.h"
#include "Engine/SimulationSystems.h"
#include "Game/GameConfig.h"
#include "General/Color.h"
#include "Lua/Bindings/LuaComponentRegistry.h"
#include "Renderer/RenderQueue.h"
#include "Systems/DamageSystem.h"
#include "Systems/KDTreeCollisionSystem.h"
#include "Systems/ObstacleBounceSystem.h"
#include "Systems/RenderPrimitiveSystem.h"
#include "Systems/RenderSpriteSystem.h"
#include "Systems/ScriptSystem.h"

namespace bench {
namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
// Horde disk cap + player orbit stay inside CullDistanceSystem's 1150 px radius so a non-lethal
// world keeps a constant population.
constexpr float kHordeMaxRadius = 800.0f;
constexpr float kPlayerOrbitRadius = 200.0f;
constexpr float kGoldenAngle = 2.39996323f;
constexpr int kEnemySize = 28;
constexpr int kImmortalHp = 100000000;
constexpr int kLethalHp = 30;

bool InFraction(const int index, const float fraction) {
  // Deterministic, well-spread membership test (Knuth multiplicative hash).
  const auto h = static_cast<std::uint32_t>(index) * 2654435761u;
  return static_cast<float>(h % 10000u) < fraction * 10000.0f;
}

EnemyBehaviorType BehaviorFor(const int index, const bool mixed) {
  if (!mixed) return EnemyBehaviorType::Chase;
  // Chase-heavy mix like dash-vs waves; fly-through/sine types are excluded because they leave
  // the arena and would be culled, shrinking the population mid-benchmark.
  static constexpr EnemyBehaviorType kMix[] = {
      EnemyBehaviorType::Chase, EnemyBehaviorType::Chase,  EnemyBehaviorType::Skitter, EnemyBehaviorType::Chase,
      EnemyBehaviorType::Orbit, EnemyBehaviorType::Chase,  EnemyBehaviorType::Charge,  EnemyBehaviorType::Chase,
      EnemyBehaviorType::Chase, EnemyBehaviorType::Skitter};
  return kMix[static_cast<size_t>(index) % std::size(kMix)];
}

// Moves the player around the arena centre (dash-vs benchmark_10k behaviour) and keeps the camera
// centred on it so render culling sees a realistic visible set.
struct PlayerDriverSystem {
  Entity player{};
  glm::vec2 center{};
  bool move = true;
  float angle = 0.0f;

  void operator()(const ContextFacade& ctx) {
    auto* registry = ctx.GetRegistry();
    auto& pos = registry->GetComponent<PositionComponent>(player);
    if (move) {
      angle += ctx.GetDeltaTime() * 0.5f;
      pos.value = center + glm::vec2(std::cos(angle), std::sin(angle)) * kPlayerOrbitRadius;
    }
    auto& camera = registry->Get<CameraComponent>();
    camera.viewport.x = pos.value.x - static_cast<float>(kWindowWidth) / 2.0f;
    camera.viewport.y = pos.value.y - static_cast<float>(kWindowHeight) / 2.0f;
  }
};

// Lethal mode: tops the enemy population back up at the perimeter each frame, like dash-vs's
// wave director, so the benchmark measures sustained spawn/despawn churn at a fixed N.
struct HordeDirectorSystem {
  BenchWorld* world = nullptr;
  int target = 0;
  std::shared_ptr<ComponentQuery<EnemySteeringComponent>> query;
  int spawned = 0;

  void operator()(const ContextFacade& ctx) {
    if (!query) query = ctx.GetRegistry()->CreateQuery<EnemySteeringComponent>();
    query->Update();
    const int deficit = target - static_cast<int>(query->GetCount());
    const int budget = std::min(deficit, std::max(32, target / 50));
    for (int i = 0; i < budget; ++i) {
      world->SpawnEnemy(world->SamplePerimeter(), target + spawned++);
    }
  }
};

}  // namespace

BenchWorld::BenchWorld(const WorldSpec& spec) : spec_(spec), rng_(spec.seed), bus_(std::make_unique<EventBus>()) {
  const float uniformSide = std::sqrt(static_cast<float>(std::max(spec_.enemies, 1))) * spec_.spacing;
  map_size_ = std::max(4000.0f, uniformSide + 1000.0f);
  center_ = glm::vec2(map_size_ / 2.0f, map_size_ / 2.0f);
  InstallSingletons();
  enemy_tag_ = registry_.TagId("enemy");
  SpawnPlayer();
}

BenchWorld::~BenchWorld() = default;

void BenchWorld::InstallSingletons() {
  auto& config = registry_.Set<GameConfig>(GameConfig());
  config.windowWidth = kWindowWidth;
  config.windowHeight = kWindowHeight;
  config.playableAreaWidth = map_size_;
  config.playableAreaHeight = map_size_;

  EngineContext ctx;
  ctx.eventBus = bus_.get();
  auto& storedCtx = registry_.Set<EngineContext>(ctx);
  storedCtx.config = &config;
  engine_bootstrap::InstallCoreSingletons(registry_, storedCtx, kWindowWidth, kWindowHeight,
                                          /*withFramePathCaches=*/true, nullptr);
  engine_bootstrap::InstallPoolAndSingletons(registry_);
}

float BenchWorld::RandomFloat(const float lo, const float hi) {
  std::uniform_real_distribution<float> dist(lo, hi);
  return dist(rng_);
}

glm::vec2 BenchWorld::SamplePosition(const int index, const int total) {
  const float jitter = spec_.spacing * 0.25f;
  const glm::vec2 noise(RandomFloat(-jitter, jitter), RandomFloat(-jitter, jitter));
  const auto n = static_cast<float>(std::max(total, 1));
  switch (spec_.layout) {
    case Layout::Horde: {
      const float radius = std::min(spec_.spacing * std::sqrt(n / std::numbers::pi_v<float>), kHordeMaxRadius);
      const float r = radius * std::sqrt((static_cast<float>(index) + 0.5f) / n);
      const float theta = static_cast<float>(index) * kGoldenAngle;
      return center_ + glm::vec2(std::cos(theta), std::sin(theta)) * r + noise;
    }
    case Layout::Uniform: {
      const int side = static_cast<int>(std::ceil(std::sqrt(n)));
      const float half = static_cast<float>(side) * spec_.spacing / 2.0f;
      const int rowIdx = index / side;
      const auto col = static_cast<float>(index % side);
      const auto row = static_cast<float>(rowIdx);
      return center_ + glm::vec2(col * spec_.spacing - half, row * spec_.spacing - half) + noise;
    }
    case Layout::Cluster: {
      constexpr int kKnots = 16;
      const int knot = index % kKnots;
      const int inKnot = index / kKnots;
      const float knotN = std::ceil(n / kKnots);
      const float knotRadius = 0.5f * spec_.spacing * std::sqrt(knotN / std::numbers::pi_v<float>);
      const float knotAngle = static_cast<float>(knot) * (2.0f * std::numbers::pi_v<float> / kKnots);
      const glm::vec2 knotCenter = center_ + glm::vec2(std::cos(knotAngle), std::sin(knotAngle)) * 600.0f;
      const float r = knotRadius * std::sqrt((static_cast<float>(inKnot) + 0.5f) / knotN);
      const float theta = static_cast<float>(inKnot) * kGoldenAngle;
      return knotCenter + glm::vec2(std::cos(theta), std::sin(theta)) * r + noise * 0.5f;
    }
  }
  return center_;
}

glm::vec2 BenchWorld::SamplePerimeter() {
  const auto& playerPos = registry_.GetComponent<PositionComponent>(player_).value;
  const float theta = RandomFloat(0.0f, 2.0f * std::numbers::pi_v<float>);
  const float r = RandomFloat(700.0f, 900.0f);
  return playerPos + glm::vec2(std::cos(theta), std::sin(theta)) * r;
}

void BenchWorld::SpawnPlayer() {
  const EntityMask playerCollides{layer::kEnemy | layer::kGem};
  player_ = registry_.CreateEntityWithBundle(
      PositionComponent(center_), GlobalTransformComponent{},
      SquarePrimitiveComponent(glm::vec2(0, 0), 5, 32.0f, 32.0f, octarine::Color{0, 255, 255, 255}, false),
      BoxColliderComponent(32, 32, glm::vec2(0, 0), false, playerCollides),
      EntityMaskComponent(EntityMask{layer::kPlayer}), HealthComponent(kImmortalHp),
      MagnetComponent{Entity{}, 0, static_cast<std::uint32_t>(layer::kGem), 120.0f, 0.0f});
}

Entity BenchWorld::SpawnEnemy(const glm::vec2 position, const int index) {
  const bool spitter = InFraction(index, spec_.spitterFraction);
  const bool trail = !spitter && InFraction(index + 7919, spec_.trailFraction);

  EnemySteeringComponent steering;
  steering.target_entity = player_;
  steering.type = spitter ? EnemyBehaviorType::ChaseSpitter : BehaviorFor(index, spec_.mixedBehaviors);
  steering.speed = 90.0f;
  steering.half_size = static_cast<float>(kEnemySize) / 2.0f;

  HealthComponent health(spec_.lethal ? kLethalHp : kImmortalHp);
  health.base_color_r = 150;
  health.base_color_g = 150;
  health.base_color_b = 165;

  const Entity enemy = registry_.CreateEntityWithBundle(
      PositionComponent(position), GlobalTransformComponent{},
      SquarePrimitiveComponent(glm::vec2(0, 0), 3, static_cast<float>(kEnemySize), static_cast<float>(kEnemySize),
                               octarine::Color{150, 150, 165, 255}, false),
      BoxColliderComponent(kEnemySize, kEnemySize, glm::vec2(0, 0), false, EntityMask{layer::kWeapon}),
      EntityMaskComponent(EntityMask{layer::kEnemy}), RigidBodyComponent(), steering,
      SeparationComponent{static_cast<float>(kEnemySize) / 2.0f + 2.0f, 1.0f}, health,
      WeaponZoneComponent{8.0f, -1, 0.5f, false, false});
  registry_.AddTag(enemy, enemy_tag_);

  if (spitter || trail) {
    ProjectileEmitterComponent emitter(trail ? glm::vec2(0.5f, 0.0f) : glm::vec2(180.0f, 180.0f), trail ? 6.0f : 4.0f,
                                       trail ? 2.5f : 2.0f, trail ? 6 : 8, EntityMask{layer::kPlayer}, EntityMask{}, "",
                                       !trail);
    emitter.target_entity = player_;
    emitter.is_hazard = trail;
    emitter.size = trail ? 44.0f : 10.0f;
    // Stagger first shots so emitters don't all fire on the same frame.
    emitter.count_down_timer = RandomFloat(0.0f, emitter.frequency);
    registry_.AddComponent(enemy, emitter);
  }
  return enemy;
}

Entity BenchWorld::SpawnGem(const glm::vec2 position) {
  return registry_.CreateEntityWithBundle(
      PositionComponent(position), GlobalTransformComponent{},
      SquarePrimitiveComponent(glm::vec2(0, 0), 2, 10.0f, 10.0f, octarine::Color{90, 230, 130, 255}, false),
      BoxColliderComponent(20, 20, glm::vec2(-5.0f, -5.0f), false, EntityMask{layer::kPlayer}),
      EntityMaskComponent(EntityMask{layer::kGem}),
      MagnetComponent{player_, static_cast<std::uint32_t>(layer::kGem), 0, 120.0f, 360.0f});
}

void BenchWorld::SpawnWeapons() {
  const float damage = spec_.lethal ? 10.0f : 1.0f;
  for (int i = 0; i < spec_.orbitWeapons; ++i) {
    const float a = static_cast<float>(i) * (2.0f * std::numbers::pi_v<float> / static_cast<float>(spec_.orbitWeapons));
    const Entity weapon = registry_.CreateEntityWithBundle(
        PositionComponent(glm::vec2(std::cos(a), std::sin(a)) * 90.0f), GlobalTransformComponent{},
        SquarePrimitiveComponent(glm::vec2(0, 0), 4, 64.0f, 64.0f, octarine::Color{255, 200, 80, 255}, false),
        BoxColliderComponent(64, 64, glm::vec2(0, 0), false, EntityMask{layer::kEnemy}),
        EntityMaskComponent(EntityMask{layer::kWeapon}), WeaponZoneComponent{damage, -1, 0.25f, false, false});
    registry_.SetParent(weapon, player_);
  }

  std::vector<Entity> targets;
  auto enemyQuery = registry_.CreateQuery<EnemySteeringComponent>();
  enemyQuery->Update();
  enemyQuery->ForEach([&](const Entity e, const EnemySteeringComponent&) { targets.push_back(e); });
  for (int i = 0; i < spec_.homingWeapons; ++i) {
    const Entity target = targets.empty() ? Entity{} : targets[static_cast<size_t>(i) % targets.size()];
    registry_.CreateEntityWithBundle(
        PositionComponent(center_ + glm::vec2(RandomFloat(-50.0f, 50.0f), RandomFloat(-50.0f, 50.0f))),
        GlobalTransformComponent{}, RigidBodyComponent(), HomingComponent{target, 600.0f, true},
        SquarePrimitiveComponent(glm::vec2(0, 0), 4, 12.0f, 12.0f, octarine::Color{255, 255, 255, 255}, false),
        BoxColliderComponent(16, 16, glm::vec2(0, 0), false, EntityMask{layer::kEnemy}),
        EntityMaskComponent(EntityMask{layer::kWeapon}), WeaponZoneComponent{damage, -1, 0.25f, false, false});
  }
}

void BenchWorld::SpawnPopulation() {
  for (int i = 0; i < spec_.enemies; ++i) {
    SpawnEnemy(SamplePosition(i, spec_.enemies), i);
  }
  const float gemRadius =
      std::min(spec_.spacing * std::sqrt(static_cast<float>(std::max(spec_.enemies, 1)) / std::numbers::pi_v<float>),
               kHordeMaxRadius);
  for (int i = 0; i < spec_.gems; ++i) {
    const float theta = RandomFloat(0.0f, 2.0f * std::numbers::pi_v<float>);
    const float r = gemRadius * std::sqrt(RandomFloat(0.0f, 1.0f));
    SpawnGem(center_ + glm::vec2(std::cos(theta), std::sin(theta)) * r);
  }
  SpawnWeapons();
}

void BenchWorld::RegisterDriver() {
  registry_.RegisterBulkSystem(PlayerDriverSystem{player_, center_, spec_.movePlayer});
  if (spec_.lethal) {
    registry_.RegisterBulkSystem(HordeDirectorSystem{this, spec_.enemies, nullptr});
  }
}

void BenchWorld::RegisterSimulation(const bool synchronousCollision) {
  const auto simulation = engine_bootstrap::RegisterSimulationSystems(registry_, bus_);
  simulation.collision.Func().SetSynchronous(synchronousCollision);
  registry_.Set<DamageSystem>(DamageSystem()).Init(&registry_, bus_);
  registry_.Set<ObstacleBounceSystem>(ObstacleBounceSystem()).Init(&registry_, bus_);
}

void BenchWorld::RegisterRenderPrep() {
  registry_.RegisterParallelSystem<GlobalTransformComponent, SpriteComponent>(RenderSpriteSystem());
  registry_.RegisterParallelSystem<SquarePrimitiveComponent, GlobalTransformComponent>(RenderPrimitiveSystem());
}

void BenchWorld::AttachLuaScripts(const std::string& luaSource, const int stride) {
  lua_ = std::make_unique<sol::state>();
  engine_bootstrap::InstallLuaLibraries(*lua_);
  engine_bootstrap::RegisterAllComponentBindings();
  auto scriptSystem = registry_.RegisterSystem<ScriptComponent>(ScriptSystem());
  scriptSystem.Func().CreateLuaBindings(*lua_);
  sol::table reg = lua_->create_named_table("registry");
  for (const auto& entry : LuaComponentRegistry::all()) {
    entry.bindRegistryAccessors(reg, &registry_);
  }

  sol::protected_function factory = lua_->script(luaSource);
  std::vector<Entity> enemies;
  auto query = registry_.CreateQuery<EnemySteeringComponent>();
  query->Update();
  query->ForEach([&](const Entity e, const EnemySteeringComponent&) { enemies.push_back(e); });
  for (size_t i = 0; i < enemies.size(); i += static_cast<size_t>(std::max(stride, 1))) {
    sol::table script = factory();
    registry_.AddComponent(enemies[i], ScriptComponent(script, script["on_update"]));
  }
}

void BenchWorld::BeginFrame() { registry_.Get<RenderQueue>().Clear(); }

void BenchWorld::EndFrame() { registry_.Get<RenderQueue>().Sort(); }

void BenchWorld::Step() {
  BeginFrame();
  registry_.Update(kDt);
  EndFrame();
}

}  // namespace bench

#endif  // OCTARINE_HAS_GAME_MODULE
