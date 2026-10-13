#include "Engine/SimulationSystems.h"

#include <functional>

#include "Components/LifetimeComponent.h"
#include "Components/PositionComponent.h"
#include "Components/ProjectileComponent.h"
#include "Components/RigidBodyComponent.h"
#include "Components/SpriteComponent.h"
#include "ECS/Registry.h"
#include "EventBus/EventBus.h"
#include "Systems/AnimationSystem.h"
#include "Systems/CombatSystem.h"
#include "Systems/CullDistanceSystem.h"
#include "Systems/EnemySteeringSystem.h"
#include "Systems/HomingSystem.h"
#include "Systems/KDTreeCollisionSystem.h"
#include "Systems/LifetimeSystem.h"
#include "Systems/MagnetSystem.h"
#include "Systems/OffScreenDespawnSystem.h"
#include "Systems/ProjectileEmitSystem.h"
#include "Systems/ProjectileLifecycleSystem.h"
#include "Systems/RetaliationSystem.h"
#include "Systems/ScriptCombatCallbackSystem.h"
#include "Systems/SeparationSystem.h"
#include "Systems/TransformSystem.h"
#include "Systems/VelocityIntegrationSystem.h"

namespace engine_bootstrap {

SimulationSystemHandles RegisterSimulationSystems(Registry& registry, const std::unique_ptr<EventBus>& eventBus) {
  registry.RegisterParallelSystem<SpriteComponent, AnimationComponent>(AnimationSystem());
  registry.RegisterParallelSystem<ProjectileComponent>(ProjectileLifecycleSystem());

  // Engine-side auto-fire: tick emitters whose frequency > 0 and spawn a projectile on expiry.
  // Running it in C++ removes one sol2 call per shooter per frame (ScriptSystem was ~56% of Update
  // at stress scale). fire_projectile(...) from Lua still works for scripted/aimed firing — those
  // emitters keep frequency 0 and are skipped here, so the two paths never double-fire one emitter.
  //
  // Registered as a serial BulkSystem: the tick spawns into the registry (pool spawn). The system
  // manages its own emitter and target queries internally, updating each exactly once per frame.
  auto projectileEmit = registry.RegisterBulkSystem(registry.Get<ProjectileEmitSystem>());

  auto steering = registry.RegisterBulkSystem(EnemySteeringSystem());
  auto homing = registry.RegisterBulkSystem(HomingSystem());
  registry.RegisterBulkSystem(CullDistanceSystem());
  auto separation = registry.RegisterBulkSystem(SeparationSystem());
  registry.RegisterBulkSystem(MagnetSystem());
  auto& combatSystem = registry.Set<CombatSystem>(CombatSystem());
  combatSystem.Init(&registry, eventBus);
  auto combatHandle = registry.RegisterBulkSystem(std::ref(combatSystem));

  auto& scriptCombatCallbackSystem = registry.Set<ScriptCombatCallbackSystem>(ScriptCombatCallbackSystem());
  scriptCombatCallbackSystem.Init(&registry, eventBus);
  auto scriptCbHandle = registry.RegisterBulkSystem(std::ref(scriptCombatCallbackSystem));
  registry.Order(scriptCbHandle).After(combatHandle);

  auto& retaliationSystem = registry.Set<RetaliationSystem>(RetaliationSystem());
  retaliationSystem.Init(registry, eventBus.get());

  // Integrate velocity into local position.
  auto velocityIntegration =
      registry.RegisterParallelSystem<PositionComponent, RigidBodyComponent>(VelocityIntegrationSystem());

  // Despawn entities (except the player) once they leave the playable area.
  registry.RegisterParallelSystem<PositionComponent, SpriteComponent>(OffScreenDespawnSystem());

  // Resolve the transform hierarchy into global positions/scales.
  auto transform = registry.RegisterBulkSystem<GlobalTransformComponent>(TransformSystem());

  auto collision = registry.RegisterBulkSystem(KDTreeCollisionSystem());
  // Store a pointer so the Lua are_colliding() query can reach IsOverlapping() without coupling
  // the module binding to the BulkSystem registration mechanism. The pointer is stable for the
  // registry's lifetime (the wrapper is owned by registry.systems_).
  registry.Set<KDTreeCollisionSystem*>(&collision.Func());

  // Lifetime system for expiring and despawning entities.
  registry.RegisterBulkSystem<LifetimeComponent>(LifetimeSystem());

  // Emit before integration so freshly-spawned projectiles integrate/transform/collide the same
  // frame — matching the spawn-timing the old ScriptSystem-driven Lua path had.
  registry.Order(steering).After(projectileEmit);
  registry.Order(homing).After(steering);
  registry.Order(separation).After(homing);
  registry.Order(velocityIntegration).After(separation);
  // Transforms resolve after velocity integration mutates local positions; collision reads
  // transform.globalPosition / globalScale, so it runs after both.
  registry.Order(transform).After(velocityIntegration);
  registry.Order(collision).After(transform).After(velocityIntegration);

  return {velocityIntegration, transform, collision};
}

}  // namespace engine_bootstrap
