#include <sol/sol.hpp>

#include "Components/HealthComponent.h"
#include "ECS/Registry.h"
#include "Engine/GameModuleHooks.h"
#include "EventBus/EventBus.h"
#include "Lua/Bindings/HealthComponentLuaBinding.h"
#include "Lua/Bindings/LuaComponentRegistry.h"
#include "Lua/LuaBindingContext.h"
#include "Systems/HealthRegenSystem.h"

namespace game_module {

void RegisterComponentBindings() { LuaComponentRegistry::registerComponent<HealthComponent>(); }

void InstallSingletons(Registry& /*registry*/) {}

void RegisterSimulationSystems(Registry& registry, const std::unique_ptr<EventBus>& /*eventBus*/,
                               const SystemId velocityIntegration) {
  auto regen = registry.RegisterBulkSystem(HealthRegenSystem{});
  registry.Order(regen).After(velocityIntegration);
}

void RegisterEventSystems(Registry& /*registry*/, const std::unique_ptr<EventBus>& /*eventBus*/) {}

void InstallLuaFunctions(sol::state& lua, LuaBindingContext& ctx) {
  sol::table mod = lua.create_named_table("minimal_module");
  mod.set_function("alive_health_count", [&ctx]() -> int {
    auto* registry = ctx.GetRegistry();
    if (registry == nullptr) {
      return 0;
    }
    int alive = 0;
    auto query = registry->CreateQuery<HealthComponent>();
    query->Update();
    query->ForEach([&alive](Entity /*entity*/, const HealthComponent& health) {
      if (!health.IsDead()) {
        ++alive;
      }
    });
    return alive;
  });
}

void RegisterDebugUI(Registry& /*registry*/) {}

}  // namespace game_module
