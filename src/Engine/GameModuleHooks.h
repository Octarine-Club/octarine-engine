#pragma once

#include <memory>
#include <sol/forward.hpp>

#include "ECS/System.h"

class EventBus;
class Registry;
class Renderer;
class LuaBindingContext;

namespace game_module {

void RegisterComponentBindings();
void InstallSingletons(Registry& registry);
void RegisterSimulationSystems(Registry& registry, const std::unique_ptr<EventBus>& eventBus,
                               SystemId velocityIntegration);
void RegisterEventSystems(Registry& registry, const std::unique_ptr<EventBus>& eventBus);
void InstallLuaFunctions(sol::state& lua, LuaBindingContext& ctx);
void RegisterDebugUI(Registry& registry);

// Optional post-scene/custom render pass registration hook. Marked weak on GNU/Clang toolchains so
// existing native game modules that do not define RegisterRenderPasses continue linking cleanly.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void RegisterRenderPasses(Registry& registry, Renderer& renderer);

inline void InvokeRegisterRenderPasses(Registry& registry, Renderer& renderer) {
#if defined(__GNUC__) || defined(__clang__)
  auto* const fn = &RegisterRenderPasses;
  if (fn != nullptr) {
    fn(registry, renderer);
  }
#else
  RegisterRenderPasses(registry, renderer);
#endif
}

}  // namespace game_module
