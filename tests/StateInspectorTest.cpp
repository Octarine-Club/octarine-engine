#include <string>

#include "Components/HealthComponent.h"
#include "Components/NameComponent.h"
#include "Components/PositionComponent.h"
#include "ECS/Registry.h"
#include "ECS/StateInspector.h"
#include "TestHarness.h"

using octarine::test::Check;

int main() {
  // Test 1: DumpSummary on empty and populated registry
  {
    Registry registry;
    const std::string summaryEmpty = octarine::inspect::StateInspector::DumpSummary(registry, 0);
    Check(summaryEmpty.find("\"entity_count\": 0") != std::string::npos, "empty summary has 0 entities");

    const Entity e1 = registry.CreateEntity();
    const Entity e2 = registry.CreateEntity();
    registry.AddComponent(e1, PositionComponent{glm::vec2(10.0f, 20.0f)});
    registry.AddComponent(e2, HealthComponent{100});

    const std::string summaryPopulated = octarine::inspect::StateInspector::DumpSummary(registry, 42);
    Check(summaryPopulated.find("\"user_entity_count\": 2") != std::string::npos,
          "populated summary has 2 user entities");
    Check(summaryPopulated.find("\"tick\": 42") != std::string::npos, "summary contains tick index");
  }

  // Test 2: DumpEntity and DumpEntities
  {
    Registry registry;
    const Entity e = registry.CreateEntity();
    registry.AddComponent(e, PositionComponent{glm::vec2(15.5f, 30.5f)});
    registry.AddComponent(e, HealthComponent{80});
    registry.AddComponent(e, NameComponent{"Player"});

    const std::string entityJson = octarine::inspect::StateInspector::DumpEntity(registry, e);
    Check(entityJson.find("\"PositionComponent\"") != std::string::npos, "DumpEntity includes PositionComponent");
    Check(entityJson.find("\"HealthComponent\"") != std::string::npos, "DumpEntity includes HealthComponent");
    Check(entityJson.find("\"NameComponent\"") != std::string::npos, "DumpEntity includes NameComponent");
    Check(entityJson.find(R"("name": "Player")") != std::string::npos, "DumpEntity preserves string property");

    const std::string allEntitiesJson = octarine::inspect::StateInspector::DumpEntities(registry);
    Check(allEntitiesJson.find("\"entities\": [") != std::string::npos, "DumpEntities lists entity array");
  }

  // Test 3: QueryComponents
  {
    Registry registry;
    const Entity e1 = registry.CreateEntity();
    const Entity e2 = registry.CreateEntity();
    registry.AddComponent(e1, PositionComponent{glm::vec2(1.0f, 2.0f)});
    registry.AddComponent(e2, HealthComponent{50});

    const std::string queryPos = octarine::inspect::StateInspector::QueryComponents(registry, {"PositionComponent"});
    Check(queryPos.find("\"matches\": 1") != std::string::npos, "QueryComponents finds matching entity");

    const std::string queryNone =
        octarine::inspect::StateInspector::QueryComponents(registry, {"PositionComponent", "HealthComponent"});
    Check(queryNone.find("\"matches\": 0") != std::string::npos, "QueryComponents handles non-overlapping query");

    const std::string queryUnknown =
        octarine::inspect::StateInspector::QueryComponents(registry, {"NonExistentComponent"});
    Check(queryUnknown.find("\"matches\": 0") != std::string::npos,
          "QueryComponents returns 0 matches for unknown component name");
  }

  // Test 4: ComputeStateHash determinism and sensitivity
  {
    Registry registry;
    const Entity e = registry.CreateEntity();
    registry.AddComponent(e, PositionComponent{glm::vec2(100.0f, 200.0f)});

    const uint64_t hash1 = octarine::inspect::StateInspector::ComputeStateHash(registry);
    const uint64_t hash2 = octarine::inspect::StateInspector::ComputeStateHash(registry);
    Check(hash1 == hash2, "ComputeStateHash is deterministic");

    registry.GetComponent<PositionComponent>(e).value.x = 101.0f;
    const uint64_t hash3 = octarine::inspect::StateInspector::ComputeStateHash(registry);
    Check(hash1 != hash3, "ComputeStateHash changes when state mutates");
  }

  // Test 5: DiffSnapshots
  {
    const std::string a = "line1\nline2\n";
    const std::string b = "line1\nline3\n";
    const std::string diffSame = octarine::inspect::StateInspector::DiffSnapshots(a, a);
    Check(diffSame.empty(), "Diff of identical snapshots is empty");

    const std::string diffDiff = octarine::inspect::StateInspector::DiffSnapshots(a, b);
    Check(!diffDiff.empty(), "Diff of different snapshots contains changes");
    Check(diffDiff.find("- line2") != std::string::npos, "Diff displays removed lines");
    Check(diffDiff.find("+ line3") != std::string::npos, "Diff displays added lines");
  }

  // Test 6: ValidateInvariants contract check
  {
    Registry registry;
    const Entity e1 = registry.CreateEntity();
    registry.AddComponent(e1, PositionComponent{glm::vec2(1.0f, 1.0f)});
    registry.ValidateInvariants();
    Check(true, "ValidateInvariants passes on healthy registry");

    registry.QueueDespawnEntity(e1);
    registry.Update(0.016f);
    registry.ValidateInvariants();
    Check(true, "ValidateInvariants passes after entity despawn");
  }

  return octarine::test::ReportSummary("StateInspectorTest");
}
