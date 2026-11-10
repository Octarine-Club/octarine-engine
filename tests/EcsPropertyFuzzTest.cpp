#include <cstdint>
#include <iostream>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "TestHarness.h"

using octarine::test::Check;
using octarine::test::CheckEq;

namespace {

struct FuzzPos {
  int x = 0;
  int y = 0;
  bool operator==(const FuzzPos& o) const { return x == o.x && y == o.y; }
};

struct FuzzVel {
  int dx = 0;
  int dy = 0;
  bool operator==(const FuzzVel& o) const { return dx == o.dx && dy == o.dy; }
};

struct FuzzStat {
  int hp = 100;
  int armor = 10;
  bool operator==(const FuzzStat& o) const { return hp == o.hp && armor == o.armor; }
};

struct OracleState {
  bool alive = false;
  std::optional<FuzzPos> pos{};
  std::optional<FuzzVel> vel{};
  std::optional<FuzzStat> stat{};
};

struct EntityHash {
  std::size_t operator()(const Entity& e) const noexcept {
    return std::hash<std::uint64_t>{}(static_cast<std::uint64_t>(e.id));
  }
};

void RunDifferentialFuzzTest(uint32_t seed, size_t numOperations) {
  Registry registry;
  std::unordered_map<Entity, OracleState, EntityHash> oracle;
  std::vector<Entity> activeEntities;
  std::vector<Entity> deadEntities;

  std::mt19937 rng(static_cast<std::mt19937::result_type>(seed));
  std::uniform_int_distribution<int> opDist(0, 9);
  std::uniform_int_distribution<int> valDist(-1000, 1000);

  for (size_t opIdx = 0; opIdx < numOperations; ++opIdx) {
    const int op = opDist(rng);

    if (op <= 2 || activeEntities.empty()) {
      // Op 0-2: Create entity
      Entity e = registry.CreateEntity();
      activeEntities.push_back(e);
      oracle[e] = OracleState{true, std::nullopt, std::nullopt, std::nullopt};

      if (valDist(rng) > 0) {
        FuzzPos pos{valDist(rng), valDist(rng)};
        registry.AddComponent(e, pos);
        oracle[e].pos = pos;
      }
      if (valDist(rng) > 200) {
        FuzzVel vel{valDist(rng), valDist(rng)};
        registry.AddComponent(e, vel);
        oracle[e].vel = vel;
      }
    } else if (op == 3) {
      // Op 3: Destroy entity (Blam)
      std::uniform_int_distribution<size_t> idxDist(0, activeEntities.size() - 1);
      const size_t targetIdx = idxDist(rng);
      Entity e = activeEntities[targetIdx];

      activeEntities[targetIdx] = activeEntities.back();
      activeEntities.pop_back();

      registry.BlamEntity(e);
      oracle[e].alive = false;
      oracle[e].pos.reset();
      oracle[e].vel.reset();
      oracle[e].stat.reset();
      deadEntities.push_back(e);

    } else if (op == 4 || op == 5) {
      // Op 4-5: Add or mutate component
      std::uniform_int_distribution<size_t> idxDist(0, activeEntities.size() - 1);
      Entity e = activeEntities[idxDist(rng)];
      int compType = std::uniform_int_distribution<int>(0, 2)(rng);

      if (compType == 0) {
        FuzzPos pos{valDist(rng), valDist(rng)};
        if (!oracle[e].pos.has_value()) {
          registry.AddComponent(e, pos);
        } else {
          registry.GetComponent<FuzzPos>(e) = pos;
        }
        oracle[e].pos = pos;
      } else if (compType == 1) {
        FuzzVel vel{valDist(rng), valDist(rng)};
        if (!oracle[e].vel.has_value()) {
          registry.AddComponent(e, vel);
        } else {
          registry.GetComponent<FuzzVel>(e) = vel;
        }
        oracle[e].vel = vel;
      } else {
        FuzzStat stat{valDist(rng), valDist(rng)};
        if (!oracle[e].stat.has_value()) {
          registry.AddComponent(e, stat);
        } else {
          registry.GetComponent<FuzzStat>(e) = stat;
        }
        oracle[e].stat = stat;
      }

    } else if (op == 6 || op == 7) {
      // Op 6-7: Remove component
      std::uniform_int_distribution<size_t> idxDist(0, activeEntities.size() - 1);
      Entity e = activeEntities[idxDist(rng)];
      int compType = std::uniform_int_distribution<int>(0, 2)(rng);

      if (compType == 0 && oracle[e].pos.has_value()) {
        registry.RemoveComponent<FuzzPos>(e);
        oracle[e].pos.reset();
      } else if (compType == 1 && oracle[e].vel.has_value()) {
        registry.RemoveComponent<FuzzVel>(e);
        oracle[e].vel.reset();
      } else if (compType == 2 && oracle[e].stat.has_value()) {
        registry.RemoveComponent<FuzzStat>(e);
        oracle[e].stat.reset();
      }

    } else {
      // Op 8-9: Spot check a random entity against Oracle
      std::uniform_int_distribution<size_t> idxDist(0, activeEntities.size() - 1);
      Entity e = activeEntities[idxDist(rng)];
      const auto& state = oracle[e];

      Check(registry.IsAlive(e), "active entity is alive in registry");
      Check(registry.HasComponent<FuzzPos>(e) == state.pos.has_value(), "FuzzPos presence matches oracle");
      if (state.pos.has_value()) {
        Check(registry.GetComponent<FuzzPos>(e) == *state.pos, "FuzzPos value matches oracle");
      }

      Check(registry.HasComponent<FuzzVel>(e) == state.vel.has_value(), "FuzzVel presence matches oracle");
      if (state.vel.has_value()) {
        Check(registry.GetComponent<FuzzVel>(e) == *state.vel, "FuzzVel value matches oracle");
      }

      Check(registry.HasComponent<FuzzStat>(e) == state.stat.has_value(), "FuzzStat presence matches oracle");
      if (state.stat.has_value()) {
        Check(registry.GetComponent<FuzzStat>(e) == *state.stat, "FuzzStat value matches oracle");
      }
    }

    // Periodic deep verification
    if ((opIdx + 1) % 5000 == 0 || opIdx == numOperations - 1) {
      registry.ValidateInvariants();

      // Verify dead entities
      for (size_t d = 0; d < std::min<size_t>(deadEntities.size(), 200); ++d) {
        Entity de = deadEntities[d];
        Check(!registry.IsAlive(de), "dead entity is not alive in registry");
      }

      // Query differential checks
      size_t posCount = 0;
      registry.CreateQuery<FuzzPos>()->ForEach([&](Entity e, const FuzzPos& pos) {
        ++posCount;
        Check(oracle[e].alive, "queried entity is alive in oracle");
        Check(oracle[e].pos.has_value(), "queried entity has pos in oracle");
        Check(pos == *oracle[e].pos, "queried pos matches oracle");
      });

      size_t expectedPosCount = 0;
      for (Entity e : activeEntities) {
        if (oracle[e].pos.has_value()) ++expectedPosCount;
      }
      CheckEq(posCount, expectedPosCount, "FuzzPos query entity count matches oracle count");

      size_t posVelCount = 0;
      registry.CreateQuery<FuzzPos, FuzzVel>()->ForEach([&](Entity e, const FuzzPos& p, const FuzzVel& v) {
        ++posVelCount;
        Check(oracle[e].pos.has_value() && oracle[e].vel.has_value(), "queried entity has both components in oracle");
        Check(p == *oracle[e].pos, "FuzzPos matches in multi-query");
        Check(v == *oracle[e].vel, "FuzzVel matches in multi-query");
      });

      size_t expectedPosVelCount = 0;
      for (Entity e : activeEntities) {
        if (oracle[e].pos.has_value() && oracle[e].vel.has_value()) ++expectedPosVelCount;
      }
      CheckEq(posVelCount, expectedPosVelCount, "multi-component query count matches oracle");
    }
  }

  Check(true, "Completed " + std::to_string(numOperations) + " differential fuzz operations");
}

}  // namespace

int main() {
  RunDifferentialFuzzTest(1337, 50000);
  return octarine::test::ReportSummary("OctarineEcsPropertyFuzzTest");
}
