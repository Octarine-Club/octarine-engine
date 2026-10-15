#include "Engine/SimulationSystems.h"

#include <functional>

#include "Components/GlobalTransformComponent.h"
#include "Components/LifetimeComponent.h"
#include "Components/PositionComponent.h"
#include "Components/RigidBodyComponent.h"
#include "Components/SpriteComponent.h"
#include "ECS/Registry.h"
#include "Engine/GameModuleHooks.h"
#include "EventBus/EventBus.h"
#include "Systems/AnimationSystem.h"
#include "Systems/KDTreeCollisionSystem.h"
#include "Systems/LifetimeSystem.h"
#include "Systems/OffScreenDespawnSystem.h"
#include "Systems/TransformSystem.h"
#include "Systems/VelocityIntegrationSystem.h"

namespace engine_bootstrap {

SimulationSystemHandles RegisterSimulationSystems(Registry& registry, const std::unique_ptr<EventBus>& eventBus) {
  registry.RegisterParallelSystem<SpriteComponent, AnimationComponent>(AnimationSystem());

  // Integrate velocity into local position.
  auto velocityIntegration =
      registry.RegisterParallelSystem<PositionComponent, RigidBodyComponent>(VelocityIntegrationSystem());

  // Game module simulation systems (ordered before velocity integration)
  game_module::RegisterSimulationSystems(registry, eventBus, velocityIntegration.Id());

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

  // Transforms resolve after velocity integration mutates local positions; collision reads
  // transform.globalPosition / globalScale, so it runs after both.
  registry.Order(transform).After(velocityIntegration);
  registry.Order(collision).After(transform).After(velocityIntegration);

  return {velocityIntegration, transform, collision};
}

}  // namespace engine_bootstrap
