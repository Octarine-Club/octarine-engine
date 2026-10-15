#pragma once

#include <memory>
#include <sol/sol.hpp>

#include "ECS/System.h"

class EventBus;
class Registry;
class LuaBindingContext;

#ifdef OCTARINE_HAS_GAME_MODULE

namespace game_module {

void RegisterComponentBindings();
void RegisterComponentInspectors();
void InstallSingletons(Registry& registry);
void RegisterSimulationSystems(Registry& registry, const std::unique_ptr<EventBus>& eventBus,
                               SystemId velocityIntegration);
void RegisterEventSystems(Registry& registry, const std::unique_ptr<EventBus>& eventBus);
void InstallLuaFunctions(sol::state& lua, LuaBindingContext& ctx);

}  // namespace game_module

#else

namespace game_module {

inline void RegisterComponentBindings() {}
inline void RegisterComponentInspectors() {}
inline void InstallSingletons(Registry& /*registry*/) {}
inline void RegisterSimulationSystems(Registry& /*registry*/, const std::unique_ptr<EventBus>& /*eventBus*/,
                                      SystemId /*velocityIntegration*/) {}
inline void RegisterEventSystems(Registry& /*registry*/, const std::unique_ptr<EventBus>& /*eventBus*/) {}
inline void InstallLuaFunctions(sol::state& /*lua*/, LuaBindingContext& /*ctx*/) {}

}  // namespace game_module

#endif
