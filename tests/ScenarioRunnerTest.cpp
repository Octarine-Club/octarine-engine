#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

#include "Components/GlobalTransformComponent.h"
#include "Components/PositionComponent.h"
#include "Components/RigidBodyComponent.h"
#include "ECS/Registry.h"
#include "ECS/StateInspector.h"
#include "Engine/EngineContext.h"
#include "EventBus/EventBus.h"
#include "Systems/TransformSystem.h"
#include "Systems/VelocityIntegrationSystem.h"
#include "TestHarness.h"

using octarine::test::Check;

namespace {

struct ScenarioStep {
  uint32_t tick = 0;
  std::function<void(Registry&)> action;
};

class HeadlessScenarioRunner {
 public:
  HeadlessScenarioRunner() {
    bus_ = std::make_unique<EventBus>();
    EngineContext ctx;
    ctx.eventBus = bus_.get();
    registry_.Set<EngineContext>(ctx);

    auto vel = registry_.RegisterParallelSystem<PositionComponent, RigidBodyComponent>(VelocityIntegrationSystem{});
    auto trans = registry_.RegisterBulkSystem<GlobalTransformComponent>(TransformSystem{});
    registry_.Order(trans).After(vel);
  }

  void AddStep(uint32_t tick, std::function<void(Registry&)> action) { steps_.push_back({tick, std::move(action)}); }

  void Run(uint32_t totalTicks, float dt = 1.0f / 60.0f) {
    for (uint32_t t = 0; t < totalTicks; ++t) {
      for (const auto& step : steps_) {
        if (step.tick == t && step.action) {
          step.action(registry_);
        }
      }
      registry_.Update(dt);
      registry_.ValidateInvariants();
      hashes_.push_back(octarine::inspect::StateInspector::ComputeStateHash(registry_));
    }
  }

  Registry& GetRegistry() { return registry_; }
  const std::vector<uint64_t>& GetHashes() const { return hashes_; }

 private:
  Registry registry_;
  std::unique_ptr<EventBus> bus_;
  std::vector<ScenarioStep> steps_;
  std::vector<uint64_t> hashes_;
};

}  // namespace

int main() {
  // Test 1: Multi-Tick Deterministic Scenario
  {
    auto makeScenario = []() {
      HeadlessScenarioRunner runner;
      Entity player{};

      runner.AddStep(0, [&player](Registry& reg) {
        player = reg.CreateEntity();
        reg.AddComponent(player, PositionComponent{glm::vec2(0.0f, 0.0f)});
        reg.AddComponent(player, RigidBodyComponent{glm::vec2(10.0f, 0.0f)});
        reg.AddComponent(player, GlobalTransformComponent{glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 1.0f), 0.0f});
      });

      runner.AddStep(10, [&player](Registry& reg) {
        // Boost velocity at tick 10
        reg.GetComponent<RigidBodyComponent>(player).velocity.x = 50.0f;
      });

      runner.Run(20);
      return runner;
    };

    auto run1 = makeScenario();
    auto run2 = makeScenario();

    Check(run1.GetHashes().size() == 20, "completed all 20 scenario ticks");
    Check(run1.GetHashes() == run2.GetHashes(), "two scenario runs generated identical hash sequence");

    const std::string summary = octarine::inspect::StateInspector::DumpSummary(run1.GetRegistry(), 20);
    Check(summary.find("\"user_entity_count\": 1") != std::string::npos, "scenario summary holds 1 user entity");
  }

  return octarine::test::ReportSummary("ScenarioRunnerTest");
}
