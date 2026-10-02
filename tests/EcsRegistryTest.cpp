// Correctness checks for the ECS Registry — the spine every system rides on. Archetype-migration
// bugs corrupt component data silently, so these assert the round-trips and transitions directly.
// gtest-free; exit code = failed-check count. Registered with ctest as EcsRegistryTest.
//
// Links the ECS core only (Registry.cpp + Logger.cpp) — no SDL/window — exactly like the
// benchmark target. Uses local POD component structs (same approach as EntityPoolBenchmark.cpp)
// so the test is independent of the real Components/ headers.

#include <stdexcept>
#include <string>
#include <vector>

#include "ECS/Query.h"  // full ComponentQuery definition for CreateQuery / ForEach
#include "ECS/Registry.h"
#include "TestHarness.h"

using octarine::test::Check;

namespace {
struct Position {
  float x = 0.0f;
  float y = 0.0f;
};
struct Velocity {
  float dx = 0.0f;
  float dy = 0.0f;
};
struct Health {
  int hp = 0;
};
}  // namespace

int main() {
  // Add / Has / Get round-trip.
  {
    Registry registry;
    const Entity e = registry.CreateEntity();
    Check(registry.IsAlive(e), "freshly created entity is alive");
    Check(!registry.HasComponent<Position>(e), "no component before AddComponent");

    registry.AddComponent(e, Position{1.0f, 2.0f});
    Check(registry.HasComponent<Position>(e), "HasComponent true after AddComponent");
    Check(registry.GetComponent<Position>(e).x == 1.0f && registry.GetComponent<Position>(e).y == 2.0f,
          "GetComponent round-trips the stored value");
  }

  // Archetype migration: adding a second component preserves the first; removing one keeps the other.
  {
    Registry registry;
    const Entity e = registry.CreateEntity();
    registry.AddComponent(e, Position{3.0f, 4.0f});
    registry.AddComponent(e, Velocity{5.0f, 6.0f});  // archetype {Position} -> {Position, Velocity}
    Check(registry.HasComponent<Position>(e) && registry.HasComponent<Velocity>(e),
          "entity holds both components after second add");
    Check(registry.GetComponent<Position>(e).x == 3.0f, "first component value survives archetype migration");
    Check(registry.GetComponent<Velocity>(e).dx == 5.0f, "second component value placed correctly");

    registry.RemoveComponent<Position>(e);  // {Position, Velocity} -> {Velocity}
    Check(!registry.HasComponent<Position>(e), "removed component is gone");
    Check(registry.HasComponent<Velocity>(e) && registry.GetComponent<Velocity>(e).dy == 6.0f,
          "untouched component intact after a removal migration");
  }

  // Re-adding an existing component assigns over the live slot (no duplicate, value replaced).
  {
    Registry registry;
    const Entity e = registry.CreateEntity();
    registry.AddComponent(e, Position{1.0f, 1.0f});
    const ArchetypeID before = registry.GetArchetypeID(e);
    registry.AddComponent(e, Position{9.0f, 9.0f});  // re-add path
    Check(registry.GetComponent<Position>(e).x == 9.0f, "re-add assigns over the existing slot");
    Check(registry.GetArchetypeID(e) == before, "re-add does not change the archetype");
  }

  // CreateEntityWithBundle lands in the same end-state as incremental adds.
  {
    Registry registry;
    const Entity bundled = registry.CreateEntityWithBundle(Position{7.0f, 8.0f}, Velocity{1.0f, 2.0f});
    Check(registry.HasComponent<Position>(bundled) && registry.HasComponent<Velocity>(bundled),
          "bundle create lands all components");
    Check(registry.GetComponent<Position>(bundled).y == 8.0f, "bundle component value correct");

    const Entity incremental = registry.CreateEntity();
    registry.AddComponent(incremental, Position{0.0f, 0.0f});
    registry.AddComponent(incremental, Velocity{0.0f, 0.0f});
    Check(registry.GetArchetypeID(bundled) == registry.GetArchetypeID(incremental),
          "bundle create reaches the same archetype as incremental adds");
  }

  // Blam dedup + deferred destruction via Update.
  {
    Registry registry;
    const Entity e = registry.CreateEntity();
    registry.AddComponent(e, Position{0.0f, 0.0f});
    registry.QueueBlamEntity(e);
    registry.QueueBlamEntity(e);  // duplicate within the frame — must collapse to one blam
    Check(registry.IsAlive(e), "queued blam is deferred until Update");
    registry.Update(1.0f / 60.0f);
    Check(!registry.IsAlive(e), "entity is destroyed after Update processes the blam queue");
    Check(!registry.HasComponent<Position>(e), "destroyed entity reports no components");
    registry.Update(1.0f / 60.0f);  // second Update must not double-process the (now empty) queue
    Check(!registry.IsAlive(e), "stale handle stays invalid across further updates");
  }

  // Queries visit exactly the matching set.
  {
    Registry registry;
    for (int i = 0; i < 3; ++i) {
      const Entity e = registry.CreateEntity();
      registry.AddComponent(e, Position{static_cast<float>(i), 0.0f});
    }
    for (int i = 0; i < 2; ++i) {
      const Entity e = registry.CreateEntity();
      registry.AddComponent(e, Velocity{0.0f, 0.0f});  // no Position — must be skipped
    }
    int seen = 0;
    const auto query = registry.CreateQuery<Position>();
    query->ForEach([&](Position&) { ++seen; });
    Check(seen == 3, "query<Position> visits exactly the entities that have Position");
  }

  // Activate / Deactivate move across the active partition without crossing archetypes.
  {
    Registry registry;
    const Entity e = registry.CreateEntity();
    registry.AddComponent(e, Position{0.0f, 0.0f});
    const ArchetypeID archetype = registry.GetArchetypeID(e);
    Check(registry.IsActive(e), "new entity starts active");

    registry.Deactivate(e);
    Check(!registry.IsActive(e) && registry.IsAlive(e), "deactivated entity is inactive but still alive");
    Check(registry.GetArchetypeID(e) == archetype, "deactivate does not change the archetype");
    int seenAfterDeactivate = 0;
    const auto query = registry.CreateQuery<Position>();
    query->ForEach([&](Position&) { ++seenAfterDeactivate; });
    Check(seenAfterDeactivate == 0, "default query skips deactivated entities");

    registry.Activate(e);
    Check(registry.IsActive(e), "reactivated entity is active again");
  }

  // Tags: string and typed, add / has / remove.
  {
    Registry registry;
    const Entity e = registry.CreateEntity();
    registry.AddTag(e, "enemy");
    Check(registry.HasTag(e, "enemy"), "string tag present after AddTag");
    Check(!registry.HasTag(e, "boss"), "unrelated string tag absent");
    registry.RemoveTag(e, "enemy");
    Check(!registry.HasTag(e, "enemy"), "string tag gone after RemoveTag");

    struct Frozen {};
    registry.AddTag<Frozen>(e);
    Check(registry.HasTag<Frozen>(e), "typed tag present after AddTag<T>");
    registry.RemoveTag<Frozen>(e);
    Check(!registry.HasTag<Frozen>(e), "typed tag gone after RemoveTag<T>");
  }

  // Singletons: Set / Get / TryGet.
  {
    Registry registry;
    Check(registry.TryGet<Health>() == nullptr, "TryGet returns nullptr before Set");
    registry.Set<Health>(Health{42});
    Check(registry.Get<Health>().hp == 42, "Get returns the Set value");
    Check(registry.TryGet<Health>() != nullptr && registry.TryGet<Health>()->hp == 42,
          "TryGet returns the live singleton after Set");
  }

  // Archetype generation bumps on a new shape, stays stable for a repeated shape.
  {
    Registry registry;
    const Entity a = registry.CreateEntity();
    const uint64_t gen0 = registry.ArchetypeGeneration();
    registry.AddComponent(a, Position{0.0f, 0.0f});  // creates archetype {Position}
    const uint64_t gen1 = registry.ArchetypeGeneration();
    Check(gen1 > gen0, "ArchetypeGeneration bumps when a new archetype is created");

    const Entity b = registry.CreateEntity();
    registry.AddComponent(b, Position{0.0f, 0.0f});  // same shape — no new archetype
    Check(registry.ArchetypeGeneration() == gen1, "ArchetypeGeneration stable for a repeated shape");
  }

  // System ordering: unconstrained registration order, After edges, lazy re-sort.
  {
    Registry registry;
    const Entity e = registry.CreateEntity();
    registry.AddComponent(e, Position{0.0f, 0.0f});

    std::string order;
    auto a = registry.RegisterSystem<Position>([&order](Position&) { order += 'a'; });
    auto b = registry.RegisterSystem<Position>([&order](Position&) { order += 'b'; });
    auto c = registry.RegisterSystem<Position>([&order](Position&) { order += 'c'; });

    registry.Update(1.0f / 60.0f);
    octarine::test::CheckEq(order, "abc", "unconstrained systems run in registration order");

    // Edge added after the first Update must trigger a re-sort: a now runs after c, while the
    // unconstrained b keeps its registration position via the SystemId tiebreak.
    order.clear();
    registry.Order(a).After(c);
    registry.Update(1.0f / 60.0f);
    octarine::test::CheckEq(order, "bca", "After edge respected; tiebreak keeps registration order");
    (void)b;
  }

  // System ordering: Before edge flips an order that registration alone would produce.
  {
    Registry registry;
    const Entity e = registry.CreateEntity();
    registry.AddComponent(e, Position{0.0f, 0.0f});

    std::string order;
    auto first = registry.RegisterSystem<Position>([&order](Position&) { order += '1'; });
    auto second = registry.RegisterSystem<Position>([&order](Position&) { order += '2'; });
    registry.Order(second).Before(first);
    registry.Update(1.0f / 60.0f);
    octarine::test::CheckEq(order, "21", "Before edge respected regardless of registration order");
  }

  // System ordering: a constraint cycle throws on Update instead of running a bogus order.
  {
    Registry registry;
    const Entity e = registry.CreateEntity();
    registry.AddComponent(e, Position{0.0f, 0.0f});

    auto a = registry.RegisterSystem<Position>([](Position&) {});
    auto b = registry.RegisterSystem<Position>([](Position&) {});
    registry.Order(a).After(b);
    registry.Order(b).After(a);
    bool threw = false;
    try {
      registry.Update(1.0f / 60.0f);
    } catch (const std::runtime_error&) {
      threw = true;
    }
    Check(threw, "ordering cycle detected and thrown on Update");
  }

  // Incremental query matching: a long-lived query updated across archetype churn must see
  // exactly the same entities as a freshly-built query (which takes the full-rebuild path).
  {
    Registry registry;
    auto incremental = registry.CreateQuery<Position>();
    incremental->Update();  // matches nothing yet; caches the current generation

    // Churn: mint several new archetypes after the query's last Update — some matching
    // (Position-bearing) and some not (Velocity-only / tag-only shapes).
    for (int i = 0; i < 5; ++i) {
      const Entity e = registry.CreateEntityWithBundle(Position{static_cast<float>(i), 0.0f});
      registry.AddTag(e, "shape_" + std::to_string(i));  // unique tag → unique archetype
    }
    const Entity v = registry.CreateEntity();
    registry.AddComponent(v, Velocity{1.0f, 1.0f});  // non-matching archetype

    incremental->Update();  // incremental append path
    auto fresh = registry.CreateQuery<Position>();
    fresh->Update();  // full-rebuild path

    int incrementalCount = 0;
    int freshCount = 0;
    incremental->ForEach([&](Position&) { ++incrementalCount; });
    fresh->ForEach([&](Position&) { ++freshCount; });
    Check(incrementalCount == 5, "incrementally-updated query sees all post-creation archetypes");
    Check(incrementalCount == freshCount, "incremental match equals a fresh full-scan match");

    // Filter change after churn forces a full re-match that honours the exclusion everywhere.
    incremental->WithoutTag("shape_0");
    incremental->Update();
    int excludedCount = 0;
    incremental->ForEach([&](Position&) { ++excludedCount; });
    Check(excludedCount == 4, "WithoutTag after churn full-rebuilds with the exclusion applied");

    // New archetypes carrying the excluded tag must not leak in through the incremental path.
    const Entity later = registry.CreateEntityWithBundle(Position{9.0f, 9.0f}, Velocity{0.0f, 0.0f});
    registry.AddTag(later, "shape_0");
    incremental->Update();
    int afterLeakCheck = 0;
    incremental->ForEach([&](Position&) { ++afterLeakCheck; });
    Check(afterLeakCheck == 4, "incremental append still applies WithoutTag exclusions");
  }

  // Empty chunk pruning: archetypes with all entities destroyed prune chunks to zero.
  {
    Registry registry;
    const Entity e = registry.CreateEntityWithBundle(Position{1.0f, 2.0f}, Velocity{3.0f, 4.0f});
    const Archetype* arch = registry.GetEntityLocation(e).archetype;
    Check(arch != nullptr, "archetype exists for bundled entity");
    Check(arch->GetChunkCount() == 1, "freshly populated archetype allocates 1 chunk");
    Check(arch->GetEntityCount() == 1, "archetype reports 1 entity");
    Check(!arch->IsEmpty(), "archetype is not empty");

    registry.BlamEntity(e);
    Check(arch->GetChunkCount() == 0, "blamming the last entity prunes chunk to zero");
    Check(arch->GetEntityCount() == 0, "archetype entity count drops to 0");
    Check(arch->IsEmpty(), "archetype reports empty after chunk pruning");

    // Adding a new entity to an empty archetype re-allocates a chunk cleanly.
    const Entity e2 = registry.CreateEntityWithBundle(Position{5.0f, 6.0f}, Velocity{7.0f, 8.0f});
    Check(registry.GetEntityLocation(e2).archetype == arch, "same component shape reuses archetype");
    Check(arch->GetChunkCount() == 1, "new entity re-allocates chunk in previously-pruned archetype");
    Check(registry.GetComponent<Position>(e2).x == 5.0f, "component readable after chunk re-allocation");
  }

  // Pooling / disabled preservation: chunks with deactivated entities are NOT pruned.
  {
    Registry registry;
    const Entity e = registry.CreateEntityWithBundle(Position{10.0f, 20.0f}, Velocity{30.0f, 40.0f});
    const Archetype* arch = registry.GetEntityLocation(e).archetype;

    // Deactivate the entity (simulating EntityPoolManager::Park).
    registry.Deactivate(e);
    Check(!registry.IsActive(e), "entity is deactivated");
    Check(arch->GetActiveCount() == 0, "active count is 0 in chunk");
    Check(arch->GetEntityCount() == 1, "entity count remains 1 in chunk");
    Check(arch->GetChunkCount() == 1, "chunk is NOT pruned while holding deactivated/pooled entities");
    Check(!arch->IsEmpty(), "archetype with deactivated entities is not empty");

    // Reactivate the entity (simulating EntityPoolManager::Spawn reuse).
    registry.Activate(e);
    Check(registry.IsActive(e), "entity reactivated successfully");
    Check(registry.GetComponent<Position>(e).x == 10.0f, "component data intact across deactivation");
    Check(arch->GetActiveCount() == 1, "active count restored to 1");

    // Blamming the deactivated entity removes it from the chunk and triggers pruning.
    registry.Deactivate(e);
    registry.BlamEntity(e);
    Check(arch->GetChunkCount() == 0, "chunk is pruned once all entities (including deactivated) are removed");
  }

  // Multi-chunk interior pruning and entity relocation:
  // When an interior chunk empties, the back chunk is moved into its slot and all its entities
  // are relocated in entity_locations_ without corruption.
  {
    Registry registry;
    const Entity sample = registry.CreateEntityWithBundle(Position{0.0f, 0.0f}, Velocity{0.0f, 0.0f});
    const Archetype* arch = registry.GetEntityLocation(sample).archetype;
    const size_t capacity = arch->GetChunkCapacity();

    // Fill chunk 0 and spill 5 entities into chunk 1.
    std::vector<Entity> chunk0Entities;
    chunk0Entities.push_back(sample);
    for (size_t i = 1; i < capacity; ++i) {
      chunk0Entities.push_back(registry.CreateEntityWithBundle(Position{static_cast<float>(i), 0.0f}, Velocity{}));
    }
    std::vector<Entity> chunk1Entities;
    for (size_t i = 0; i < 5; ++i) {
      chunk1Entities.push_back(
          registry.CreateEntityWithBundle(Position{100.0f + static_cast<float>(i), 0.0f}, Velocity{}));
    }

    Check(arch->GetChunkCount() == 2, "2 chunks allocated across capacity boundary");
    Check(arch->GetEntityCount() == capacity + 5, "total entities matches capacity + 5");

    // Destroy all entities in chunk 0.
    for (const Entity e : chunk0Entities) {
      registry.BlamEntity(e);
    }

    // Chunk 0 emptied, chunk 1 was moved to index 0, and chunk 1 was popped.
    Check(arch->GetChunkCount() == 1, "chunk count dropped from 2 to 1 after chunk 0 emptied");
    Check(arch->GetEntityCount() == 5, "remaining entity count is 5");

    // Verify all 5 entities from former chunk 1 are now valid at chunk 0 and have intact data.
    for (size_t i = 0; i < chunk1Entities.size(); ++i) {
      const Entity e = chunk1Entities[i];
      Check(registry.IsAlive(e), "relocated entity is alive");
      const auto loc = registry.GetEntityLocation(e);
      Check(loc.chunkIndex == 0, "relocated entity has chunkIndex == 0");
      Check(registry.GetComponent<Position>(e).x == 100.0f + static_cast<float>(i),
            "relocated entity components are intact");
    }

    // Query iterates the relocated entities properly.
    int seenCount = 0;
    const auto query = registry.CreateQuery<Position, Velocity>();
    query->ForEach([&](const Position& p, const Velocity&) {
      Check(p.x >= 100.0f, "query visits relocated entity in new chunk slot");
      ++seenCount;
    });
    Check(seenCount == 5, "query visits all 5 relocated entities");

    // Clean up remaining entities -> prunes last chunk.
    for (const Entity e : chunk1Entities) {
      registry.BlamEntity(e);
    }
    Check(arch->GetChunkCount() == 0, "final chunk pruned after all entities blammed");
  }

  // Intermediate archetype pruning: adding components sequentially does not leave empty chunks.
  {
    Registry registry;
    const Entity e = registry.CreateEntity();
    registry.AddComponent(e, Position{1.0f, 2.0f});
    const Archetype* posArch = registry.GetEntityLocation(e).archetype;
    Check(posArch->GetChunkCount() == 1, "intermediate {Position} archetype has 1 chunk");

    // Transition to {Position, Velocity} empties {Position}.
    registry.AddComponent(e, Velocity{3.0f, 4.0f});
    const Archetype* finalArch = registry.GetEntityLocation(e).archetype;
    Check(finalArch->GetChunkCount() == 1, "target {Position, Velocity} archetype has 1 chunk");
    Check(posArch->GetChunkCount() == 0, "intermediate {Position} chunk was pruned upon entity migration");

    registry.BlamEntity(e);
    Check(finalArch->GetChunkCount() == 0, "final archetype chunk pruned after blam");
  }

  return octarine::test::Result();
}
