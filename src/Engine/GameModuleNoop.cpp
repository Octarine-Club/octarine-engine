#include <sol/sol.hpp>

#include "Engine/GameModuleHooks.h"

namespace game_module {

void RegisterComponentBindings() {}
void InstallSingletons(Registry& /*registry*/) {}
void RegisterSimulationSystems(Registry& /*registry*/, const std::unique_ptr<EventBus>& /*eventBus*/,
                               SystemId /*velocityIntegration*/) {}
void RegisterEventSystems(Registry& /*registry*/, const std::unique_ptr<EventBus>& /*eventBus*/) {}
void InstallLuaFunctions(sol::state& /*lua*/, LuaBindingContext& /*ctx*/) {}
void RegisterDebugUI(Registry& /*registry*/) {}

}  // namespace game_module
