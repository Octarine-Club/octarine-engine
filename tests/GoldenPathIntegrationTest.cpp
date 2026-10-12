#include <cmath>
#include <glm/glm.hpp>
#include <memory>
#include <utility>
#include <vector>

#include "Components/BoxColliderComponent.h"
#include "Components/EntityMaskComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/LifetimeComponent.h"
#include "Components/PositionComponent.h"
#include "Components/RigidBodyComponent.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "ECS/StateInspector.h"
#include "Engine/EngineContext.h"
#include "EventBus/EventBus.h"
#include "Events/CollisionBatchEvent.h"
#include "Systems/KDTreeCollisionSystem.h"
#include "Systems/LifetimeSystem.h"
#include "Systems/TransformSystem.h"
#include "Systems/VelocityIntegrationSystem.h"
#include "TestHarness.h"

using octarine::test::Check;
using octarine::test::CheckEq;

namespace {

struct CollisionTracker {
  int eventCount = 0;
  std::vector<std::pair<Entity, Entity>> pairs;

  void OnCollision(const CollisionBatchEvent& evt) {
    ++eventCount;
    for (const auto& p : evt.pairs) {
      pairs.push_back(p);
    }
  }

  [[nodiscard]] size_t TotalPairs() const { return pairs.size(); }
};

}  // namespace

int main() {
  // Scenario 1: Golden Path Simulation Pipeline (Integration -> Transform -> Collision)
  {
    Registry registry;
    auto bus = std::make_unique<EventBus>();
    EngineContext ctx;
    ctx.eventBus = bus.get();
    registry.Set<EngineContext>(ctx);

    CollisionTracker tracker;
    auto sub = bus->SubscribeEvent<CollisionTracker, CollisionBatchEvent>(&tracker, &CollisionTracker::OnCollision);

    auto velSystem =
        registry.RegisterParallelSystem<PositionComponent, RigidBodyComponent>(VelocityIntegrationSystem{});
    auto transformSystem = registry.RegisterBulkSystem<GlobalTransformComponent>(TransformSystem{});
    auto collisionSystem = registry.RegisterBulkSystem(KDTreeCollisionSystem(true));
    registry.RegisterBulkSystem<LifetimeComponent>(LifetimeSystem{});

    registry.Order(transformSystem).After(velSystem);
    registry.Order(collisionSystem).After(transformSystem);

    // Initial state: Bullet at (0, 0) moving towards (60, 0) at 120 px/s
    const Entity bullet = registry.CreateEntity();
    registry.AddComponent(bullet, PositionComponent{glm::vec2(0.0f, 0.0f)});
    registry.AddComponent(bullet, RigidBodyComponent{glm::vec2(120.0f, 0.0f)});
    registry.AddComponent(bullet, GlobalTransformComponent{glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 1.0f), 0.0f});
    registry.AddComponent(bullet, BoxColliderComponent{16, 16});
    registry.AddComponent(bullet, EntityMaskComponent{});

    // Target at (50, 0) stationary
    const Entity target = registry.CreateEntity();
    registry.AddComponent(target, PositionComponent{glm::vec2(50.0f, 0.0f)});
    registry.AddComponent(target, GlobalTransformComponent{glm::vec2(50.0f, 0.0f), glm::vec2(1.0f, 1.0f), 0.0f});
    registry.AddComponent(target, BoxColliderComponent{32, 32});
    registry.AddComponent(target, EntityMaskComponent{});

    registry.ValidateInvariants();
    Check(registry.IsAlive(bullet) && registry.IsAlive(target), "both entities alive initially");

    // Tick 1: dt = 0.1s -> bullet moves from 0 to 12. No collision with target at 50.
    registry.Update(0.1f);
    registry.ValidateInvariants();

    const auto& bulletPos1 = registry.GetComponent<PositionComponent>(bullet);
    Check(std::abs(bulletPos1.value.x - 12.0f) < 1e-3f, "velocity integrated to x = 12");
    CheckEq(tracker.TotalPairs(), size_t{0}, "no collision pairs on first tick");

    // Tick 2: dt = 0.35s -> bullet moves by 42 to x = 54. Overlaps target at 50!
    registry.Update(0.35f);
    registry.ValidateInvariants();

    const auto& bulletPos2 = registry.GetComponent<PositionComponent>(bullet);
    Check(std::abs(bulletPos2.value.x - 54.0f) < 1e-3f, "velocity integrated to x = 54");
    Check(tracker.eventCount > 0, "collision detected and emitted synchronously");

    bool foundPair = false;
    for (const auto& [a, b] : tracker.pairs) {
      if ((a == bullet && b == target) || (a == target && b == bullet)) {
        foundPair = true;
        break;
      }
    }
    Check(foundPair, "collision batch contains bullet and target");

    // Deterministic state hashing check
    const uint64_t hash = octarine::inspect::StateInspector::ComputeStateHash(registry);
    Check(hash != 0, "state hash computed successfully");
  }

  // Scenario 2: Lifetime Expiration and Despawn
  {
    Registry registry;
    registry.RegisterBulkSystem<LifetimeComponent>(LifetimeSystem{});

    const Entity tempEntity = registry.CreateEntity();
    registry.AddComponent(tempEntity, PositionComponent{glm::vec2(1.0f, 1.0f)});
    registry.AddComponent(tempEntity, LifetimeComponent{0.2f});

    registry.ValidateInvariants();
    Check(registry.IsAlive(tempEntity), "temp entity alive before expiration");

    // Tick 0.1s: remaining 0.1s
    registry.Update(0.1f);
    registry.ValidateInvariants();
    Check(registry.IsAlive(tempEntity), "temp entity still alive after partial tick");

    // Tick 0.15s: expired and despawned
    registry.Update(0.15f);
    registry.ValidateInvariants();
    Check(!registry.IsAlive(tempEntity), "temp entity despawned after lifetime expiration");
  }

  return octarine::test::ReportSummary("GoldenPathIntegrationTest");
}
