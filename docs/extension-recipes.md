# Octarine Engine — Extension Recipes

Step-by-step procedures for extending Octarine Engine's core ECS, scripting, systems, and game modules while preserving catalog sync and quality gates.

---

## 1. Adding an ECS Component
1. Create a POD struct in `src/Components/<Name>Component.h` (data only, no complex logic; adhere to descending size packing).
2. Create sol2 bindings in `src/Lua/Bindings/<Name>ComponentLuaBinding.h`.
3. Register the binding in [`src/Lua/Bindings/RegisterAllBindings.cpp`](src/Lua/Bindings/RegisterAllBindings.cpp).
4. Run `LuaApiSmokeTest` to regenerate and update [`lua_api.smoke.lua`](lua_api.smoke.lua), [`components.json`](components.json), and [`modules.json`](modules.json). Commit the updated catalogs.

---

## 2. Adding a Simulation System
1. Create `src/Systems/<Name>System.h` and `src/Systems/<Name>System.cpp`.
2. Register the `.cpp` file under `octarine_systems` in [`src/CMakeLists.txt`](src/CMakeLists.txt).
3. Register the system in `Game::Setup()` in [`src/Game/Game.cpp`](src/Game/Game.cpp) using the appropriate tier:
   - `RegisterSystem<...>(...)` — Serial execution; required if the system mutates registry structure (spawns/despawns entities, adds/removes components).
   - `RegisterParallelSystem<...>(...)` — Parallel execution across chunks; safe for component reads/writes without structural changes.
   - `RegisterBulkSystem<...>(...)` — Bulk archetype/component execution.
4. Regenerate system and event catalogs:
   ```bash
   python3 scripts/gen_api_catalogs.py
   ```
   Commit updates to [`systems.json`](systems.json) and [`events.json`](events.json).

---

## 3. Adding a Lua Global Module
1. Create `src/Lua/Modules/<Name>ModuleLuaBinding.h` and `.cpp`.
2. Register the `.cpp` under `octarine_lua` in [`src/CMakeLists.txt`](src/CMakeLists.txt).
3. Add the descriptor to `kLuaModules` in [`src/Lua/Modules/RegisterAllModules.cpp`](src/Lua/Modules/RegisterAllModules.cpp).
4. Run `LuaApiSmokeTest` to regenerate [`modules.json`](modules.json) and [`lua_api.smoke.lua`](lua_api.smoke.lua).

---

## 4. Adding an Event
1. Define the event struct in `src/Events/<Name>Event.h` inheriting from `Event` (include `"EventBus/Event.h"`).
2. Emit using `event_bus_.EmitEvent<NameEvent>(...)` and subscribe using `event_bus_.SubscribeEvent<NameEvent, HandlerClass>(...)`.
3. Regenerate catalogs:
   ```bash
   python3 scripts/gen_api_catalogs.py
   ```

---

## 5. Using a Native C++ Game Module (`OCTARINE_GAME_MODULE_DIR`)
Game-specific C++ components, systems, events, Lua bindings, tests, and benchmarks live in the game project's repository (e.g. `<game>/native/`) rather than the engine:
1. Pass `-DOCTARINE_GAME_MODULE_DIR=/path/to/game/native` at CMake configure time.
2. Define `octarine_library(octarine_game_module ...)` in `<game>/native/CMakeLists.txt` and implement the six `game_module::*` hooks declared in [`src/Engine/GameModuleHooks.h`](src/Engine/GameModuleHooks.h).
3. Place game-module unit tests in `<game>/native/tests/CMakeLists.txt` (using `octarine_add_engine_test` / `octarine_add_core_test`); they are automatically discovered when `OCTARINE_ENABLE_TESTS=ON` and grouped under `octarine_game_module_tests`.
4. Place game-module benchmarks in `<game>/native/benchmarks/CMakeLists.txt` (attaching sources to `OctarineBenchmarks` via `target_sources`); they are automatically included when `OCTARINE_ENABLE_BENCHMARKS=ON`.
See the `octarine-native` skill for complete templates and guidelines.

> [!WARNING]
> **Catalog Drift is Fatal in CI:** CI enforces zero drift on `components.json`, `modules.json`, `systems.json`, `events.json`, and `lua_api.smoke.lua`. Always run `scripts/octarine-verify.sh --full` to verify catalog sync before pushing.
