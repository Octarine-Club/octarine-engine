#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <random>
#include <vector>

#include "Components/BoxColliderComponent.h"
#include "Components/EntityMaskComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/PositionComponent.h"
#include "Components/RigidBodyComponent.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "ECS/StateInspector.h"
#include "Engine/EngineContext.h"
#include "EventBus/EventBus.h"
#include "General/SimRandom.h"
#include "General/ThreadPool.h"
#include "Systems/KDTreeCollisionSystem.h"
#include "Systems/TransformSystem.h"
#include "Systems/VelocityIntegrationSystem.h"
#include "TestHarness.h"

using octarine::test::Check;

namespace {

uint64_t RunFuzzSimulation(uint32_t seed, int entityCount, int tickCount, bool synchronousCollision = true) {
  Registry registry;
  auto bus = std::make_unique<EventBus>();
  EngineContext ctx;
  ctx.eventBus = bus.get();
  registry.Set<EngineContext>(ctx);

  auto vel = registry.RegisterParallelSystem<PositionComponent, RigidBodyComponent>(VelocityIntegrationSystem{});
  auto trans = registry.RegisterBulkSystem<GlobalTransformComponent>(TransformSystem{});
  auto coll = registry.RegisterBulkSystem(KDTreeCollisionSystem(synchronousCollision));

  registry.Order(trans).After(vel);
  registry.Order(coll).After(trans);

  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> posDist(-500.0f, 500.0f);
  std::uniform_real_distribution<float> velDist(-50.0f, 50.0f);
  std::uniform_real_distribution<float> sizeDist(16.0f, 64.0f);

  for (int i = 0; i < entityCount; ++i) {
    const Entity e = registry.CreateEntity();
    const float px = posDist(rng);
    const float py = posDist(rng);
    const float vx = velDist(rng);
    const float vy = velDist(rng);
    const float sz = sizeDist(rng);

    registry.AddComponent(e, PositionComponent{glm::vec2(px, py)});
    registry.AddComponent(e, RigidBodyComponent{glm::vec2(vx, vy)});
    registry.AddComponent(e, GlobalTransformComponent{glm::vec2(px, py), glm::vec2(1.0f, 1.0f), 0.0f});
    registry.AddComponent(e, BoxColliderComponent{static_cast<int>(sz), static_cast<int>(sz)});
    registry.AddComponent(e, EntityMaskComponent{});
  }

  const float dt = 1.0f / 60.0f;
  for (int t = 0; t < tickCount; ++t) {
    registry.Update(dt);

    // Invariant 1: Structural invariants
    registry.ValidateInvariants();

    // Invariant 2: Entity conservation
    const auto userEntities = registry.GetUserEntities();
    Check(userEntities.size() == registry.GetUserEntityCount(), "user entities match user entity count");

    // Invariant 3: No NaN or Inf
    for (const Entity e : userEntities) {
      if (registry.HasComponent<PositionComponent>(e)) {
        const auto& p = registry.GetComponent<PositionComponent>(e);
        Check(!std::isnan(p.value.x) && !std::isinf(p.value.x), "position x is finite");
        Check(!std::isnan(p.value.y) && !std::isinf(p.value.y), "position y is finite");
      }
      if (registry.HasComponent<GlobalTransformComponent>(e)) {
        const auto& gt = registry.GetComponent<GlobalTransformComponent>(e);
        Check(!std::isnan(gt.position.x) && !std::isinf(gt.position.x), "global position x is finite");
        Check(!std::isnan(gt.position.y) && !std::isinf(gt.position.y), "global position y is finite");
      }
    }
  }

  return octarine::inspect::StateInspector::ComputeStateHash(registry);
}

}  // namespace

int main() {
  // Test 1: Metamorphic Fuzz Run
  {
    const uint64_t hash1 = RunFuzzSimulation(1337, 20, 30);
    Check(hash1 != 0, "first fuzz run completed with non-zero hash");

    // Test 2: Deterministic Replay Equivalence (identical seed -> identical hash)
    const uint64_t hash2 = RunFuzzSimulation(1337, 20, 30);
    Check(hash1 == hash2, "identical seed produces identical simulation state hash");

    // Test 3: Sensitivity to Seed Mutation
    const uint64_t hashDiffSeed = RunFuzzSimulation(9999, 20, 30);
    Check(hash1 != hashDiffSeed, "different seed produces different simulation state hash");
  }

  // Test 4: Deterministic simulation mode (ThreadPool force-serial)
  {
    ThreadPool::SetForceSerial(true);
    Check(ThreadPool::IsForceSerial(), "force-serial flag reads back true");

    size_t batches = 0;
    size_t covered = 0;
    ThreadPool::ParallelChunks(10000, [&](size_t /*batch*/, size_t begin, size_t end) {
      ++batches;
      covered += end - begin;
    });
    Check(batches == 1 && covered == 10000, "ParallelChunks runs as a single serial batch in force-serial mode");

    // > kDefaultSerialBelowEntities so ParallelForEach would otherwise fan out, and the async
    // collision path (which depends on worker timing) is requested but must be forced synchronous.
    const uint64_t a = RunFuzzSimulation(4242, 800, 40, /*synchronousCollision=*/false);
    const uint64_t b = RunFuzzSimulation(4242, 800, 40, /*synchronousCollision=*/false);
    Check(a == b, "force-serial mode replays a large async-collision simulation bit-identically");
    ThreadPool::SetForceSerial(false);
    Check(!ThreadPool::IsForceSerial(), "force-serial flag reads back false");
  }

  // Test 5: Seedable engine simulation RNG
  {
    sim_random::Seed(101);
    const float r1 = sim_random::Float01();
    const float r2 = sim_random::Float01();
    sim_random::Seed(101);
    Check(sim_random::Float01() == r1 && sim_random::Float01() == r2,
          "sim_random replays the same stream after reseeding");
    Check(r1 >= 0.0F && r1 <= 1.0F, "sim_random::Float01 stays in [0, 1]");
  }

  return octarine::test::ReportSummary("SimulationFuzzTest");
}
