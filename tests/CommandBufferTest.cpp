#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "ECS/CommandBuffer.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "TestHarness.h"

using octarine::test::Check;
using octarine::test::CheckEq;

namespace {

struct Position {
  float x = 0.0f;
  float y = 0.0f;
};

struct Velocity {
  float dx = 0.0f;
  float dy = 0.0f;
};

struct TestTag {};

void TestBasicPlayback() {
  Registry registry;
  CommandBuffer cmdBuffer(16);

  const Entity e1 = registry.CreateEntity();
  const Entity e2 = registry.CreateEntity();

  cmdBuffer.AddComponent(e1, Position{10.0f, 20.0f});
  cmdBuffer.AddTag<TestTag>(e1);
  cmdBuffer.AddComponent(e2, Velocity{1.0f, -1.0f});

  // Not applied yet before Playback
  Check(!registry.HasComponent<Position>(e1), "e1 lacks Position before Playback");
  Check(!registry.HasTag<TestTag>(e1), "e1 lacks TestTag before Playback");

  cmdBuffer.Playback(&registry);

  Check(registry.HasComponent<Position>(e1), "e1 has Position after Playback");
  CheckEq(registry.GetComponent<Position>(e1).x, 10.0f, "e1 Position.x correct");
  Check(registry.HasTag<TestTag>(e1), "e1 has TestTag after Playback");
  Check(registry.HasComponent<Velocity>(e2), "e2 has Velocity after Playback");

  // Remove tag and component via CommandBuffer
  cmdBuffer.RemoveComponent<Position>(e1);
  cmdBuffer.RemoveTag<TestTag>(e1);
  cmdBuffer.Playback(&registry);

  Check(!registry.HasComponent<Position>(e1), "e1 Position removed via Playback");
  Check(!registry.HasTag<TestTag>(e1), "e1 TestTag removed via Playback");

  // Despawn via CommandBuffer
  cmdBuffer.EmplaceDespawn(e2);
  cmdBuffer.Playback(&registry);
  registry.Update(0.0f);  // Flushes pending destruction
  Check(!registry.IsAlive(e2), "e2 despawned via Playback");

  registry.ValidateInvariants();
}

void TestOverflowBuffer() {
  Registry registry;
  // Initialize with tiny capacity = 4 to force overflow into overflow_buffer
  CommandBuffer cmdBuffer(4);

  constexpr size_t kTotalEntities = 32;
  std::vector<Entity> entities;
  entities.reserve(kTotalEntities);

  for (size_t i = 0; i < kTotalEntities; ++i) {
    Entity e = registry.CreateEntity();
    entities.push_back(e);
    cmdBuffer.AddComponent(e, Position{static_cast<float>(i), static_cast<float>(i * 2)});
    if (i % 2 == 0) {
      cmdBuffer.EmplaceBlam(e);
    }
  }

  cmdBuffer.Playback(&registry);
  registry.Update(0.0f);  // Flushes blam entities

  for (size_t i = 0; i < kTotalEntities; ++i) {
    Entity e = entities[i];
    if (i % 2 == 0) {
      Check(!registry.IsAlive(e), "even entities blammed through overflow buffer");
    } else {
      Check(registry.IsAlive(e), "odd entities survived");
      Check(registry.HasComponent<Position>(e), "odd entities have Position component");
      CheckEq(registry.GetComponent<Position>(e).x, static_cast<float>(i), "Position.x preserved through overflow");
    }
  }

  registry.ValidateInvariants();
}

void TestDuplicateDestruction() {
  Registry registry;
  CommandBuffer cmdBuffer(16);

  const Entity e = registry.CreateEntity();
  registry.AddComponent(e, Position{5.0f, 5.0f});

  // Queue multiple Blam and Despawn on the exact same entity
  cmdBuffer.EmplaceBlam(e);
  cmdBuffer.EmplaceDespawn(e);
  cmdBuffer.EmplaceBlam(e);
  cmdBuffer.Playback(&registry);

  registry.Update(0.0f);
  Check(!registry.IsAlive(e), "entity cleanly destroyed despite duplicate destruction commands");

  registry.ValidateInvariants();
}

void TestStaleHandleSafety() {
  Registry registry;
  CommandBuffer cmdBuffer(16);

  const Entity e = registry.CreateEntity();
  registry.AddComponent(e, Position{1.0f, 1.0f});

  // Destroy immediately
  registry.BlamEntity(e);
  Check(!registry.IsAlive(e), "entity is dead");

  // Attempt deferred operations on stale entity handle
  cmdBuffer.AddComponent(e, Velocity{10.0f, 20.0f});
  cmdBuffer.RemoveComponent<Position>(e);
  cmdBuffer.AddTag<TestTag>(e);
  cmdBuffer.RemoveTag<TestTag>(e);

  // Playback must not throw or crash on stale entity
  bool ok = true;
  try {
    cmdBuffer.Playback(&registry);
  } catch (...) {
    ok = false;
  }
  Check(ok, "stale entity playback gracefully ignored without exceptions");
  registry.ValidateInvariants();
}

void TestConcurrentRecording() {
  Registry registry;
  constexpr size_t kThreadCount = 8;
  constexpr size_t kEntitiesPerThread = 100;
  constexpr size_t kTotalEntities = kThreadCount * kEntitiesPerThread;

  std::vector<Entity> entities;
  entities.reserve(kTotalEntities);
  for (size_t i = 0; i < kTotalEntities; ++i) {
    entities.push_back(registry.CreateEntity());
  }

  CommandBuffer cmdBuffer(32);  // Intentionally smaller than total to stress overflow mutex

  std::vector<std::thread> workers;
  workers.reserve(kThreadCount);
  for (size_t t = 0; t < kThreadCount; ++t) {
    workers.emplace_back([&cmdBuffer, &entities, t]() {
      const size_t start = t * kEntitiesPerThread;
      const size_t end = start + kEntitiesPerThread;
      for (size_t i = start; i < end; ++i) {
        cmdBuffer.AddComponent(entities[i], Position{static_cast<float>(i), 100.0f});
        if (i % 4 == 0) {
          cmdBuffer.EmplaceBlam(entities[i]);
        }
      }
    });
  }

  for (auto& w : workers) {
    w.join();
  }

  cmdBuffer.Playback(&registry);
  registry.Update(0.0f);

  size_t aliveCount = 0;
  size_t blammedCount = 0;
  for (size_t i = 0; i < kTotalEntities; ++i) {
    if (registry.IsAlive(entities[i])) {
      ++aliveCount;
      Check(registry.HasComponent<Position>(entities[i]), "alive entity has Position");
    } else {
      ++blammedCount;
    }
  }

  CheckEq(blammedCount, kTotalEntities / 4, "exact number of blammed entities matched");
  CheckEq(aliveCount, kTotalEntities - (kTotalEntities / 4), "exact number of survivors matched");

  registry.ValidateInvariants();
}

void TestDeterministicPlaybackOrderAndClearCaptures() {
  // Verify that out-of-order (reverse entity ID) recording plays back sorted by (EntityID, sequence)
  // and that Channel::Clear() releases captured shared_ptr payloads when Clear() is called without Playback().
  Registry regA;
  Registry regB;
  CommandBuffer cmdA(16);
  CommandBuffer cmdB(16);

  std::vector<Entity> entsA;
  std::vector<Entity> entsB;
  for (int i = 0; i < 8; ++i) {
    entsA.push_back(regA.CreateEntity());
    entsB.push_back(regB.CreateEntity());
  }

  // Record in forward order into cmdA and reverse order into cmdB
  for (const Entity entity : entsA) {
    cmdA.AddComponent(entity, Position{1.0f, 0.0f});
    cmdA.AddComponent(entity, Position{2.0f, 0.0f});  // sequence 2 overwrites sequence 1 for same entity
    cmdA.EmplaceDespawn(entity);
  }
  for (size_t i = entsB.size(); i > 0; --i) {
    const size_t idx = i - 1;
    cmdB.AddComponent(entsB[idx], Position{1.0f, 0.0f});
    cmdB.AddComponent(entsB[idx], Position{2.0f, 0.0f});
    cmdB.EmplaceDespawn(entsB[idx]);
  }

  cmdA.Playback(&regA);
  cmdB.Playback(&regB);
  for (size_t i = 0; i < entsA.size(); ++i) {
    CheckEq(regA.GetComponent<Position>(entsA[i]).x, 2.0f, "same-entity op sequence preserved in forward record");
    CheckEq(regB.GetComponent<Position>(entsB[i]).x, 2.0f, "same-entity op sequence preserved in reverse record");
  }
  regA.Update(0.0f);
  regB.Update(0.0f);

  // Next created entity IDs must match because despawns were replayed in identical deterministic EntityID order
  const Entity nextA = regA.CreateEntity();
  const Entity nextB = regB.CreateEntity();
  CheckEq(nextA.id, nextB.id, "deterministic CommandBuffer despawn order yields identical recycled entity IDs");

  // Verify Clear() releases std::function captures in buffer
  struct SharedPayload {
    std::shared_ptr<int> token;
  };
  auto token = std::make_shared<int>(99);
  const Entity live = regA.CreateEntity();
  cmdA.AddComponent(live, SharedPayload{token});
  Check(token.use_count() > 1, "token captured inside CommandBuffer deferred channel");
  cmdA.Clear();
  CheckEq(token.use_count(), 1L, "CommandBuffer::Clear releases captured std::function payloads in buffer");
}

}  // namespace

int main() {
  TestBasicPlayback();
  TestOverflowBuffer();
  TestDuplicateDestruction();
  TestStaleHandleSafety();
  TestConcurrentRecording();
  TestDeterministicPlaybackOrderAndClearCaptures();
  return octarine::test::ReportSummary("OctarineCommandBufferTest");
}
