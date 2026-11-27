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

// Optional post-scene/custom render pass registration hook using an explicit callback so native
// game modules that do not register custom passes continue linking cleanly on every toolchain
// (including MSVC, which lacks weak function symbols).
using RenderPassHookFn = void (*)(Registry&, Renderer&);

inline RenderPassHookFn& RenderPassHookStorage() noexcept {
  static RenderPassHookFn fn = nullptr;
  return fn;
}

inline void SetRenderPassHook(const RenderPassHookFn fn) noexcept { RenderPassHookStorage() = fn; }

inline void InvokeRegisterRenderPasses(Registry& registry, Renderer& renderer) {
  if (const RenderPassHookFn fn = RenderPassHookStorage(); fn != nullptr) {
    fn(registry, renderer);
  }
}

}  // namespace game_module
