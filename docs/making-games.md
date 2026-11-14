# Making Games with Octarine Engine

A practical guide for game developers covering project setup, configuration, ECS entities, collisions, scripting, scenes, assets, audio, camera, UI, tilemaps, CI, and optional native C++ game modules.

The companion example project, [Octarine-Engine-Example](https://github.com/Octarine-Club/octarine-engine-example), demonstrates every engine feature in a runnable project. For the C++ engine reference, see the **[published Doxygen docs](https://octarine-club.github.io/octarine-engine/)**.

---

## Table of Contents

1. [Prerequisites](#1-prerequisites)
2. [Project Structure](#2-project-structure)
3. [Configuration](#3-configuration)
4. [Core Concepts](#4-core-concepts)
5. [Your First Entity](#5-your-first-entity)
6. [Collision and Callbacks](#6-collision-and-callbacks)
7. [Scripting Entities](#7-scripting-entities)
8. [Scenes](#8-scenes)
9. [Assets](#9-assets)
10. [Audio](#10-audio)
11. [Camera](#11-camera)
12. [UI, Tilemaps, Saves, and Dev Mode](#12-ui-tilemaps-saves-and-dev-mode)
13. [Game CI](#13-game-ci)
14. [Native C++ Game Modules](#14-native-c-game-modules)
15. [Further Reading](#15-further-reading)

---

## 1. Prerequisites

Before writing game code you need a built copy of the engine (`OctarineEngine`).

### Option A — Download a versioned release (recommended)

Pre-built SDK archives are published on every [GitHub Release](https://github.com/Octarine-Club/octarine-engine/releases):

| Asset (`player` dev runtime / `ship` hardened build) | Platform |
|---|---|
| `OctarineEngine-{version}-linux-{player\|ship}.tar.gz` | Linux x64 |
| `OctarineEngine-{version}-windows-{player\|ship}.zip` | Windows x64 |
| `OctarineEngine-{version}-macos-{player\|ship}.tar.gz` | macOS |

```bash
# Linux / macOS
tar -xzf OctarineEngine-0.2.4-linux-player.tar.gz
./OctarineEngine path/to/MyGame

# Windows (PowerShell)
Expand-Archive OctarineEngine-0.2.4-windows-player.zip
.\OctarineEngine.exe path/to/MyGame
```

### Option B — Build from source

Requires CMake 3.15+, a C++20 compiler (MSVC 2022, GCC 12+, or Clang 14+), and vcpkg (manifest mode).

```bash
git clone https://github.com/Octarine-Club/octarine-engine
cd octarine-engine
cmake --preset player-debug
cmake --build build/player-debug
./build/player-debug/bin/debug/OctarineEngine path/to/MyGame --dev
```

---

## 2. Project Structure

Scaffold a new standalone project directly from the engine repository:

```bash
./scripts/octarine-new.sh --name "My Game" --package-id com.studio.mygame --dir ./MyGame
```

Optional: run `./scripts/octarine-init-build.sh` inside a game project directory to initialize build configuration against a local engine checkout or installed SDK.

A minimal project needs `config.ini` and `game.lua`. A typical project layout:

```text
MyGame/
├── config.ini              # window settings, startup script
├── project.ini             # release/store packaging metadata
├── game.lua                # entry point — runs first
├── scenes/                 # scene files (title.lua, gameplay.lua)
├── scripts/                # reusable Lua modules and entity scripts
├── ui/                     # .oxml layout templates and .oss stylesheets
├── images/                 # .png / .bmp textures + optional .meta sidecars
├── fonts/                  # .ttf fonts
├── sounds/                 # .wav / .ogg audio
├── tilemaps/               # LDtk .ldtk maps + tilesets
└── types/                  # generated octarine_api.lua on --dev launch
```

---

## 3. Configuration

### `config.ini` — runtime settings

`config.ini` lives at the project root:

```ini
Title=My Awesome Game
StartupScript=game.lua
BaseResolutionWidth=1280
BaseResolutionHeight=720
DefaultWindowWidth=1280
DefaultWindowHeight=720
DefaultWindowMode=windowed       # 'windowed', 'borderless', or 'exclusive'
DefaultPresentationMode=expand   # 'expand', 'letterbox', 'integer_scale', or 'stretch'
DefaultScalingMode=nearest       # 'nearest', 'linear', or 'pixelart'
FpsTarget=60                     # frame-rate cap; 0 = uncapped (default 60)
AngleUnit=degrees                # 'degrees' (default) or 'radians'
```

`AngleUnit` selects the unit at every Lua boundary (`rotation` in entity tables, `set_rotation()`, and the `rotation` component's `value`). Internal storage stays radians. Because it defaults to `degrees`, projects written in radians must declare `AngleUnit=radians`.

### `project.ini` — packaging and release identity

`project.ini` is the source of truth read by CMake packaging and engine version checks:

```ini
name           = My Awesome Game
package_id     = com.studio.myawesomegame
version_name   = 1.0.0
version_code   = 1
vendor         = My Studio
description    = A short description for store listings.
icon           = images/icon.png
engine_version = 0.2.4
```

| Key | Required for shipping | Notes |
|---|---|---|
| `name` | Yes | Display name |
| `package_id` | Yes | Reverse-DNS identifier (`com.studio.mygame`) |
| `version_name` | Yes | SemVer string |
| `version_code` | No | Monotonic integer; defaults to `1` |
| `vendor` | No | Studio / publisher name |
| `description` | No | One-line summary |
| `icon` | No | Source image for icon generation |
| `engine_version` | No | Checked by CI and CMake configure |

---

## 4. Core Concepts

- **Entities & Components:** Octarine uses an archetype-based Entity-Component System (ECS). Entities are 64-bit generational handles; components are plain data structs (`position`, `sprite`, `rigidbody`, etc.).
- **Lua Gameplay Layer:** Scripts interact through free globals (`load_entity`, `load_scene`, `play_sound`, `blam`, `log`), namespace modules (`input.*`, `ui.*`, `tilemap.*`, `storage.*`, `settings.*`, `registry.*`, `dev.*`, `ImGui.*`), and `script` component callbacks (`on_update`, `on_collision`, `on_collision_exit`, `on_debug_gui`, `on_click`).
- **Native Escape Hatch:** Compute-heavy workloads can attach optional C++ components and systems via [`#14. Native C++ Game Modules`](#14-native-c-game-modules).

---

## 5. Your First Entity

Pass a table to `load_entity` from `game.lua` or any scene script:

```lua
load_entity({
    tag = "player",
    name = "Player",
    components = {
        transform = {
            position = { x = 100, y = 100 },
            scale    = { x = 1.0, y = 1.0 },
            rotation = 0.0,
        },
        sprite = {
            texture_asset_id = "player-texture",
            width  = 32,
            height = 32,
            layer  = 2,
        },
        rigidbody = {
            velocity = { x = 0, y = 0 },
        },
        box_collider = {
            width  = 32,
            height = 32,
        },
    },
})
```

See [`docs/ecs-components.md`](ecs-components.md) for all 39 registered components and `transform` spawn shorthand fields.

---

## 6. Collision and Callbacks

Octarine supports five collider components resolved by `KDTreeCollisionSystem` using a spatial broadphase and Separating Axis Theorem (SAT) narrowphase: `box_collider`, `circle_collider`, `capsule_collider`, `triangle_collider`, and `line_collider`.

### Collision masks

The engine bitwise-ANDs an entity's collider `collision_mask` against the other entity's `entity_mask`; a non-zero result registers contact:

```lua
entity_mask  = { value = 2 },
box_collider = { width = 32, height = 32, collision_mask = 1 },
```

### Handling collisions in Lua

`ScriptCollisionSystem` dispatches first-contact and separation events to `on_collision` and `on_collision_exit`:

```lua
load_entity({
    components = {
        transform    = { position = { x = 50, y = 300 } },
        sprite       = { texture_asset_id = "bullet", width = 8, height = 8, layer = 3 },
        rigidbody    = { velocity = { x = 250, y = 0 } },
        lifetime     = { duration = 4.0 },
        entity_mask  = { value = 4 },
        box_collider = { width = 8, height = 8, collision_mask = 2 },
        script = {
            damage = 25,
            on_collision = function(self, entity, other)
                blam(entity)
            end,
        },
    },
})
```

For large entity counts, handle `CollisionBatchEvent` and `CollisionExitBatchEvent` directly in a native C++ system.

---

## 7. Scripting Entities

Attach inline tables (`script = { on_update = function(self, entity, dt) ... end }`) or hot-reloadable file-backed tables (`script = { source = "scripts/player.lua" }`) to entities. See [`docs/lua-scripting.md`](lua-scripting.md) for callbacks, `registry.*`, `storage.*`, `settings.*`, and input APIs.

---

## 8. Scenes

Call `load_scene("scenes/gameplay.lua")` to queue a deferred scene swap at the end of the current frame step, or `reload_scene()` during development. See [`docs/scenes.md`](scenes.md) for declarative vs. procedural scene files, asset preloading, and lifecycle guarantees.

---

## 9. Assets

Preload assets per scene with `acquire_scene_assets` or load individually via `load_asset`:

```lua
load_asset({ type = "texture", id = "player",    file = "images/player.png" })
load_asset({ type = "font",    id = "main-font", file = "fonts/arial.ttf", font_size = 16 })
load_asset({ type = "sound",   id = "jump-sfx",  file = "sounds/jump.wav" })
```

Bake assets and generate `asset_manifest.lua` cleanly in CI or pre-release via:

```bash
./OctarineEngine path/to/MyGame --startup-mode bake
```

See [`docs/asset-pipeline.md`](asset-pipeline.md) for `.meta` sidecars, atlas packing, and audio normalization.

---

## 10. Audio

```lua
play_sound("jump-sfx")
play_sound("music-gameplay", 0.6)
```

For spatial attenuation, attach `audio_listener` to the camera/player entity and `audio_source` to emitters:

```lua
audio_listener = {}

audio_source = {
    clip_id = "enemy-growl",
    loop    = true,
    volume  = 0.8,
    spatial = true,
}
```

---

## 11. Camera

Attach `camera_follow = {}` to the target entity, query or update zoom via `get_camera_position()`, `get_camera_zoom()`, `set_camera_zoom(zoom)`, and bound scrolling with `set_game_map_dimensions(width_px, height_px)`. Render HUD elements in screen space by setting `fixed = true` on `sprite`, `square`, or `text_label`.

---

## 12. UI, Tilemaps, Saves, and Dev Mode

- **Declarative & Procedural UI (`ui.*`):** Author XML-like `.oxml` templates and CSS-like `.oss` stylesheets with reactive `ui.observable()` bindings and `ui.register_widget()`, or compose layouts in Lua with `ui.anchor`, `ui.flex`, `ui.scroll`, and `ui.focus`. See [`docs/ui-framework.md`](ui-framework.md) and [`docs/ui-layout.md`](ui-layout.md).
- **Infinite Chunked Tilemaps (`tilemap.*`):** Stream `.ldtk` chunks or Lua-generated chunks around the camera, query IntGrid masks, cast 2D DDA rays, resolve circle/AABB collisions, and spawn persistent props. See [`docs/tilemaps.md`](tilemaps.md).
- **Saves & User Preferences (`storage.*`, `settings.*`):** Store save files under the OS save directory with `storage.write_table` / `read_table`, and manage persistent user preferences in `user_settings.ini` with `settings.define` / `get` / `set` / `save`. See [`docs/lua-scripting.md`](lua-scripting.md#5-sandboxed-save-storage-storage-and-user-settings-settings).
- **Developer Tools & LuaLS Stubs (`--dev`, `dev.*`):** Launch with `--dev` to enable Dear ImGui overlays, `dev.*` custom debug panels, live hot reload, and automatic IDE autocomplete generation at `<assets>/types/octarine_api.lua`. See [`docs/dev-tools.md`](dev-tools.md).

---

## 13. Game CI

Use the `setup-engine` composite action in game repositories to download matching pre-built SDK binaries and validate assets or headless test scenarios:

```yaml
# .github/workflows/ci.yml
jobs:
  ci:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - uses: Octarine-Club/octarine-engine/.github/actions/setup-engine@v0.2.4
        with:
          engine-version: '0.2.4'
          token: ${{ secrets.GITHUB_TOKEN }}

      - run: OctarineEngine . --startup-mode bake
```

Set `engine_version = 0.2.4` in `project.ini` so CMake packaging and CI match the pinned SDK release. `setup-engine` exports `engine-path` and `engine-bin` outputs for cross-platform `ubuntu-latest`, `windows-latest`, and `macos-latest` matrices.

---

## 14. Native C++ Game Modules

For CPU-intensive gameplay logic (thousands of units, custom spatial grids, native combat pipelines), compile a game-specific C++ library directly into the engine via `-DOCTARINE_GAME_MODULE_DIR=/path/to/MyGame/native`.

### 1. Directory Layout in Your Game Project

```text
MyGame/
├── config.ini
├── project.ini
├── game.lua
└── native/
    ├── CMakeLists.txt
    ├── GameModule.cpp
    ├── Components/
    │   └── HealthComponent.h
    ├── Systems/
    │   ├── CombatSystem.h
    │   └── CombatSystem.cpp
    ├── Events/
    │   └── DamageDealtEvent.h
    ├── Lua/Bindings/
    │   └── HealthComponentLuaBinding.h
    ├── tests/
    │   ├── CMakeLists.txt
    │   └── CombatSystemTest.cpp
    └── benchmarks/
        ├── CMakeLists.txt
        └── SystemBenchmarks.cpp
```

### 2. Defining `octarine_game_module` (`native/CMakeLists.txt`)

When `-DOCTARINE_GAME_MODULE_DIR` is set, CMake defines `OCTARINE_HAS_GAME_MODULE` on `octarine_core` and links `octarine_game_module` into `octarine_engine`:

```cmake
# MyGame/native/CMakeLists.txt
octarine_library(octarine_game_module
    GameModule.cpp
    Systems/CombatSystem.cpp
)

target_include_directories(octarine_game_module PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(octarine_game_module PUBLIC
    octarine_core
    octarine_assets
    octarine_renderer
    octarine_lua
    octarine_systems
)
```

### 3. Implementing the Game Module Hooks (`native/GameModule.cpp`)

Implement all six hooks declared in [`src/Engine/GameModuleHooks.h`](../src/Engine/GameModuleHooks.h):

```cpp
// MyGame/native/GameModule.cpp
#include "Engine/GameModuleHooks.h"

#include "Components/HealthComponent.h"
#include "ECS/Registry.h"
#include "EventBus/EventBus.h"
#include "Lua/Bindings/HealthComponentLuaBinding.h"
#include "Lua/Bindings/LuaComponentRegistry.h"
#include "Lua/LuaBindingContext.h"
#include "Systems/CombatSystem.h"

namespace game_module {

void RegisterComponentBindings() {
  LuaComponentRegistry::registerComponent<HealthComponent>();
}

void InstallSingletons(Registry& registry) {
  (void)registry;
}

void RegisterSimulationSystems(Registry& registry, const std::unique_ptr<EventBus>& eventBus,
                               SystemId velocityIntegration) {
  (void)eventBus;
  // Order systems before or after velocityIntegration via registry.Order(...).After(...).
  (void)registry;
  (void)velocityIntegration;
}

void RegisterEventSystems(Registry& registry, const std::unique_ptr<EventBus>& eventBus) {
  auto combat = registry.RegisterSystem<CombatSystem>(&registry, eventBus);
  (void)combat;
}

void InstallLuaFunctions(sol::state& lua, LuaBindingContext& ctx) {
  (void)lua;
  (void)ctx;
}

void RegisterDebugUI(Registry& registry) {
  (void)registry;
}

}  // namespace game_module
```

> [!IMPORTANT]
> When subscribing to `EventBus` inside a native system, pass `<OwnerClass, EventType>` to `eventBus->SubscribeEvent<CombatSystem, CollisionBatchEvent>(this, &CombatSystem::OnCollision)` and store the returned `[[nodiscard]] SubscriptionHandle` on the class instance.

### 4. Adding Game Module Unit Tests (`native/tests/CMakeLists.txt`)

`octarine_add_engine_test(TARGET CTEST_NAME SOURCE)` creates the test executable and registers it with CTest automatically (`add_test` is built into the macro):

```cmake
# MyGame/native/tests/CMakeLists.txt
octarine_add_engine_test(OctarineCombatSystemTest CombatSystemTest
    CombatSystemTest.cpp
)
target_link_libraries(OctarineCombatSystemTest PRIVATE octarine_game_module)
```

### 5. Adding Game Module Benchmarks (`native/benchmarks/CMakeLists.txt`)

When `OCTARINE_ENABLE_BENCHMARKS=ON`, CMake includes `benchmarks/CMakeLists.txt` after defining `OctarineBenchmarks`:

```cmake
# MyGame/native/benchmarks/CMakeLists.txt
target_sources(OctarineBenchmarks PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/SystemBenchmarks.cpp"
)
target_include_directories(OctarineBenchmarks PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
```

### 6. Building, Testing, and Running with a Game Module

```bash
cmake --preset player-debug \
  -DOCTARINE_GAME_MODULE_DIR=/path/to/MyGame/native \
  -DOCTARINE_ENABLE_TESTS=ON
cmake --build build/player-debug

./build/player-debug/bin/debug/OctarineEngine /path/to/MyGame --dev
ctest --test-dir build/player-debug --output-on-failure
OCTARINE_GAME_MODULE_DIR=/path/to/MyGame/native ./scripts/octarine-verify.sh --fast
```

---

## 15. Further Reading

| Guide | Topic |
|---|---|
| [`docs/ecs-architecture.md`](ecs-architecture.md) | Archetype SoA storage, queries, DAG scheduling, and invariants |
| [`docs/ecs-components.md`](ecs-components.md) | All 39 components — fields, Lua keys, and usertype methods |
| [`docs/systems.md`](systems.md) | Built-in systems and execution order |
| [`docs/events.md`](events.md) | `EventBus` and built-in engine events |
| [`docs/lua-scripting.md`](lua-scripting.md) | Full Lua scripting guide, `storage.*`, and `settings.*` |
| [`docs/input-and-controllers.md`](input-and-controllers.md) | Action binding, canonical gamepad buttons, analog sticks, and UI focus |
| [`docs/scenes.md`](scenes.md) | Scene file schema and lifecycle |
| [`docs/ui-framework.md`](ui-framework.md) | Declarative `.oxml`/`.oss`, observables, and custom widgets |
| [`docs/ui-layout.md`](ui-layout.md) | Procedural canvas, anchor, flex, clip, nine-slice, and scroll layout |
| [`docs/tilemaps.md`](tilemaps.md) | Infinite LDtk chunked tilemaps, DDA raycasting, and prop streaming |
| [`docs/dev-tools.md`](dev-tools.md) | `--dev`, `dev.*`, LuaLS stubs, `--headless`, and `--dev-listen` |
| [`docs/asset-pipeline.md`](asset-pipeline.md) | `.meta` sidecars, baking, atlases, and audio normalization |
| [`docs/profiling.md`](profiling.md) | Profiling build, PerfUtils, and `OctarineBenchmarks` |
| [`docs/security.md`](security.md) | OCPK encryption, Lua bytecode protection, and sandboxing |
| [`docs/device-builds.md`](device-builds.md) | Desktop and experimental Android shipping builds |
| [`lua_api.smoke.lua`](../lua_api.smoke.lua) | Auto-generated EmmyLua stub (load in LuaLS; refreshed on `--dev` into `types/octarine_api.lua`) |
