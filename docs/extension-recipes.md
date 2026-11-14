# Extension Recipes

Step-by-step procedures for extending Octarine Engine's core ECS, scripting, systems, and game modules while preserving catalog sync and quality gates.

---

## 1. Adding an ECS Component

1. Create a POD struct in `src/Components/<Name>Component.h` (adhere to descending member-size packing; see [`lua-scripting.md`](lua-scripting.md#8-adding-component-methods-engine-contributors--native-game-modules) for method rules).
2. Create `sol2` bindings in `src/Lua/Bindings/<Name>ComponentLuaBinding.h` specializing `LuaBinding<T>`.
3. Register `LuaComponentRegistry::registerComponent<NameComponent>()` in the matching split binding TU:
   - [`src/Lua/Bindings/RegisterTransformCoreBindings.cpp`](../src/Lua/Bindings/RegisterTransformCoreBindings.cpp)
   - [`src/Lua/Bindings/RegisterRenderBindings.cpp`](../src/Lua/Bindings/RegisterRenderBindings.cpp)
   - [`src/Lua/Bindings/RegisterPhysicsAudioScriptBindings.cpp`](../src/Lua/Bindings/RegisterPhysicsAudioScriptBindings.cpp)
   - [`src/Lua/Bindings/RegisterUIBindings.cpp`](../src/Lua/Bindings/RegisterUIBindings.cpp)
   (See [`src/Lua/Bindings/RegisterAllBindings.cpp`](../src/Lua/Bindings/RegisterAllBindings.cpp) for how the split TUs are invoked.)
4. Run `ctest --test-dir build/player-debug -R LuaApiSmokeTest` to regenerate [`lua_api.smoke.lua`](../lua_api.smoke.lua), [`components.json`](../components.json), and [`modules.json`](../modules.json) (and run `python3 scripts/gen_api_catalogs.py` whenever adding or removing systems or events).

---

## 2. Adding a Simulation System

1. Create `src/Systems/<Name>System.h` and `src/Systems/<Name>System.cpp`.
2. Register the `.cpp` file under `octarine_systems` in [`src/CMakeLists.txt`](../src/CMakeLists.txt).
3. Register the system in [`src/Engine/SimulationSystems.cpp`](../src/Engine/SimulationSystems.cpp) (for simulation systems) or `Game::Setup()` in [`src/Game/Game.cpp`](../src/Game/Game.cpp) (for core engine subsystems) using the appropriate tier and `registry.Order(...).After(...)`:
   - `RegisterSystem<...>(...)` — Serial execution on the main thread; may perform structural mutations directly.
   - `RegisterParallelSystem<...>(...)` — Parallel execution across chunks; defer structural changes (spawn/destroy, add/remove components) via `CommandBuffer` (`registry.GetCommandBuffer()`).
   - `RegisterBulkSystem<...>(...)` — Bulk chunk execution tier.
4. Regenerate system and event catalogs:

```bash
python3 scripts/gen_api_catalogs.py
```

Commit updates to [`systems.json`](../systems.json) and [`events.json`](../events.json).

---

## 3. Adding a Lua Global Module

1. Create `src/Lua/Modules/<Name>ModuleLuaBinding.h` and `.cpp`.
2. Register the `.cpp` under `octarine_lua` in [`src/CMakeLists.txt`](../src/CMakeLists.txt).
3. Add the descriptor to `kLuaModules` in [`src/Lua/Modules/RegisterAllModules.cpp`](../src/Lua/Modules/RegisterAllModules.cpp).
4. Run `LuaApiSmokeTest` and `python3 scripts/gen_api_catalogs.py` to regenerate [`modules.json`](../modules.json) and [`lua_api.smoke.lua`](../lua_api.smoke.lua).

---

## 4. Adding an Event

1. Define the event struct in `src/Events/<Name>Event.h` inheriting from `Event` (`#include "EventBus/Event.h"`).
2. Emit via `event_bus_->EmitEvent<NameEvent>(...)`.
3. Subscribe using `event_bus_->SubscribeEvent<HandlerClass, NameEvent>(this, &HandlerClass::OnName)` (owner type first, event type second) and store the returned `[[nodiscard]] SubscriptionHandle` on the subscribing object so the subscription remains active for its lifetime.
4. Regenerate catalogs:

```bash
python3 scripts/gen_api_catalogs.py
```

---

## 5. Using a Native C++ Game Module (`OCTARINE_GAME_MODULE_DIR`)

Game-specific C++ components, systems, events, Lua bindings, debug UI panels, tests, and benchmarks belong in the game project repository (for example `<game>/native/`) rather than inside the engine:

1. Pass `-DOCTARINE_GAME_MODULE_DIR=/path/to/game/native` when configuring CMake.
2. Define `octarine_library(octarine_game_module ...)` in `<game>/native/CMakeLists.txt` and implement the six `game_module::*` hooks declared in [`src/Engine/GameModuleHooks.h`](../src/Engine/GameModuleHooks.h):
   - `RegisterComponentBindings()` — call `LuaComponentRegistry::registerComponent<T>()` for custom components.
   - `InstallSingletons(Registry&)` — install game-specific registry singletons before systems run.
   - `RegisterSimulationSystems(Registry&, const std::unique_ptr<EventBus>&, SystemId velocityIntegration)` — register simulation systems in the DAG relative to `velocityIntegration`.
   - `RegisterEventSystems(Registry&, const std::unique_ptr<EventBus>&)` — subscribe event-driven game systems.
   - `InstallLuaFunctions(sol::state&, LuaBindingContext&)` — bind custom game Lua APIs.
   - `RegisterDebugUI(Registry&)` — register custom ImGui debug panels/watches in `DebugMenuRegistry`.
3. Place game-module unit tests in `<game>/native/tests/CMakeLists.txt` using `octarine_add_engine_test(TARGET CTEST_NAME SOURCE)`; they are discovered when `OCTARINE_ENABLE_TESTS=ON` and grouped under `octarine_game_module_tests`.
4. Place game-module benchmarks in `<game>/native/benchmarks/CMakeLists.txt` (attaching sources to `OctarineBenchmarks` via `target_sources`); they are included automatically when `OCTARINE_ENABLE_BENCHMARKS=ON`.

See [Native C++ Game Modules in `docs/making-games.md`](making-games.md#14-native-c-game-modules) for complete C++ and CMake examples.

> [!WARNING]
> **Catalog Drift Fails CI:** CI enforces zero drift on `components.json`, `modules.json`, `systems.json`, `events.json`, and `lua_api.smoke.lua`. Run `./scripts/octarine-verify.sh --fast` (or `--full`) and `python3 scripts/gen_api_catalogs.py --check` before pushing.
