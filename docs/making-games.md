# Making Games with Octarine Engine

A practical guide for game developers. It covers how to set up a project, the
core concepts you'll use every day, and where to go for deeper reference.

The companion example project,
[Octarine-Engine-Example](https://github.com/Octarine-Club/octarine-engine-example),
is a small but complete game that exercises every feature described here — run
it alongside this guide whenever an example would help.

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
12. [Using the Editor](#12-using-the-editor)
13. [Game CI](#13-game-ci)
14. [Native C++ Game Modules](#14-native-c-game-modules)
15. [Further Reading](#15-further-reading)

---

## 1. Prerequisites

Before writing game code you need a built copy of the engine.

### Option A — Download a versioned release (recommended)

Pre-built engine binaries are published as assets on every
[GitHub Release](https://github.com/Octarine-Club/octarine-engine/releases).

1. Open the release matching your target engine version (e.g. `v0.1.0`).
2. Download the SDK binary for your platform:

| Asset | Platform |
|---|---|
| `OctarineEngine-{version}-linux.tar.gz` | Linux x64 |
| `OctarineEngine-{version}-windows.zip` | Windows x64 |
| `OctarineEngine-{version}-macos.tar.gz` | macOS (arm64) |

3. Extract the archive and run the binary directly — no install step required:

```bash
# Linux / macOS
tar -xzf OctarineEngine-0.1.0-linux.tar.gz
./OctarineEngine path/to/MyGame

# Windows (PowerShell)
Expand-Archive OctarineEngine-0.1.0-windows.zip
.\OctarineEngine.exe path/to/MyGame
```

### Option B — Build from source

Requires CMake 3.15+, a C++20-capable compiler (MSVC 2022, GCC 12+, or Clang 14+),
and vcpkg (the engine uses manifest mode and manages its own dependencies).

```bash
git clone https://github.com/Octarine-Club/octarine-engine
cd octarine-engine
cmake --preset player-debug
cmake --build build/player-debug
```

The resulting binary is `build/player-debug/bin/debug/OctarineEngine` (Linux/macOS)
or `build\player-debug\bin\debug\OctarineEngine.exe` (Windows).

**Run against your project**

```bash
./build/player-debug/bin/debug/OctarineEngine path/to/MyGame
```

The engine takes your game's root directory as its only positional argument. The
`config.ini` there tells it what startup script to run.

---

## 2. Project Structure

A minimal project needs only `config.ini` and `game.lua`. A more typical layout:

```
MyGame/
├── config.ini              # window settings, startup script
├── game.lua                # entry point — runs first
├── scenes/
│   ├── title.lua
│   ├── gameplay.lua
│   └── game_over.lua
├── scripts/
│   └── lib/                # reusable Lua helpers
│       ├── input_map.lua
│       └── player_controller.lua
├── images/                 # .png / .jpg textures
├── fonts/                  # .ttf fonts
├── sounds/                 # .wav / .ogg audio
└── tilemaps/               # .map files + tileset PNGs
```

There are no enforced path conventions beyond `config.ini` and the startup
script — organise the rest however suits your project.

---

## 3. Configuration

### `config.ini` — runtime window settings

`config.ini` lives at the project root and controls window setup:

```ini
Title=My Awesome Game
StartupScript=game.lua
DefaultWindowWidth=1280
DefaultWindowHeight=720
DefaultScalingMode=nearest      # 'nearest' for pixel art, 'linear' for smooth
FpsTarget=60                    # frame-rate cap; 0 = uncapped (default 60)
AngleUnit=degrees               # 'degrees' (default) or 'radians' for authored angles
```

`AngleUnit` picks the unit you write and read angles in — `rotation` in component
tables, `set_rotation()`, the `rotation` component's `value`, and the editor
inspectors. The engine always stores and composes in radians; the unit is a
conversion applied at those boundaries, so it costs nothing at runtime and
changing it never alters engine behaviour, only how your numbers are spelled.

**It defaults to `degrees`.** A project written against radians must say
`AngleUnit=radians` explicitly — otherwise a `rotation` of `1.57` is read as
1.57°, not a quarter turn, with no error to point at. The effective unit is
logged at startup so you can confirm which one is live.

It is global to the project. A number in a scene file or a shared Lua library
means whatever this key says, so a snippet copied between two projects with
different settings will silently change meaning. Pick one early and leave it.

`StartupScript` is the first Lua file that runs. Everything else — scenes,
assets, entities — is loaded from there.

### `project.ini` — packaging and release identity

`project.ini` is the single source of truth for your game's release identity.
It is read by the engine's CMake packaging helper, Android Gradle build, and
the engine version check. The format is flat `key = value` (no sections).

```ini
name          = My Awesome Game
package_id    = com.studio.myawesomegame    # reverse-DNS, required for shipping
version_name  = 1.0.0                       # semver displayed in stores / About
version_code  = 1                           # monotonic integer for store updates
vendor        = My Studio
description   = A short description for store listings.
icon          = images/icon.png             # source PNG for icon generation
engine_version = 0.1.0                     # engine version this project targets
```

| Key | Required for shipping | Notes |
|---|---|---|
| `name` | Yes | Display name |
| `package_id` | Yes | Reverse-DNS (e.g. `com.studio.mygame`) |
| `version_name` | Yes | SemVer string |
| `version_code` | No | Monotonic int; defaults to 1 |
| `vendor` | No | Studio / publisher name |
| `description` | No | One-line store summary |
| `icon` | No | Source image for icon generation |
| `engine_version` | No | Declared engine version; checked at package time |

The `engine_version` field is used by the `setup-engine` action in game CI (see
[Game CI](#game-ci)) and checked at CMake configure time — a mismatch emits a
warning so you know when a game project is being built against a newer or older
engine than intended.

---

## 4. Core Concepts

### Entities and Components

Octarine uses an **Entity-Component System (ECS)**. The key ideas:

- An **entity** is just an ID — a number with no data of its own.
- **Components** are plain data structs attached to entities (`position`,
  `sprite`, `rigidbody`, etc.).
- **Systems** are the engine's update loops — they query for entities with a
  given set of components and act on them each frame.

You never subclass anything or register callbacks on a base class. Instead you
compose entities from components and attach a `script` component when an entity
needs custom per-frame behaviour.

### Lua is the game layer

All game logic lives in Lua. The engine exposes its entire surface through:

- **Free globals** — `load_entity`, `load_scene`, `play_sound`, `get_position`,
  `set_position`, `blam`, `log`, etc.
- **Namespace tables** — `input.*`, `ImGui.*`, `registry.*`
- **The `script` component** — attach `on_update`, `on_collision`, `on_collision_exit`,
  and `on_debug_gui` callbacks to any entity.

You do not need to touch C++ to make a game, though performance-critical games can
optionally attach a [Native C++ Game Module](#14-native-c-game-modules).

### Scenes

A scene is a Lua file. `load_scene("scenes/gameplay.lua")` replaces the current
scene with a new one. Scenes can be declarative (return a table the engine
processes) or procedural (build the scene themselves). See [Scenes](#8-scenes).

---

## 5. Your First Entity

Pass a table to `load_entity` from anywhere in your Lua code:

```lua
load_entity({
    tag = "player",         -- optional group label
    name = "Player",        -- optional display name
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
    }
})
```

For the full list of available components and their fields see
[`docs/ecs-components.md`](ecs-components.md).

---

## 6. Collision and Callbacks

Add `box_collider` (or `circle_collider`) to any entity that should participate
in collision detection. The engine resolves overlaps every frame using a
median-cut broadphase and Separating Axis Theorem (SAT) narrowphase, then fires
enter and exit events.

### Collision masks

Masks let you control which entity groups can collide with each other. The engine
performs a bitwise AND between an entity's `collision_mask` (on `box_collider` or
`circle_collider`) and the other entity's `entity_mask`; a non-zero result means
the overlap registers.

```lua
-- entity_mask groups the entity into one or more collision layers
entity_mask  = { value = 2 }                                          -- layer 2 = "enemies"

-- box_collider filters which layers this entity reacts to
box_collider = { width = 32, height = 32, collision_mask = 1 }        -- reacts to layer 1 ("player")
```

### Handling collisions in Lua (`on_collision` / `on_collision_exit`)

`ScriptCollisionSystem` automatically forwards first-contact and separation events
to the `on_collision` and `on_collision_exit` callbacks on an entity's `script`
component:

```lua
-- Projectile that despawns on hit or after 4 seconds
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
                blam(entity) -- destroy projectile on impact
            end,
        },
    }
})

-- Target that tracks HP in its script table
load_entity({
    components = {
        transform    = { position = { x = 500, y = 300 } },
        sprite       = { texture_asset_id = "enemy", width = 32, height = 32, layer = 2 },
        entity_mask  = { value = 2 },
        box_collider = { width = 32, height = 32, collision_mask = 4 },
        script = {
            hp = 100,
            on_collision = function(self, entity, other)
                self.hp = self.hp - 25
                if self.hp <= 0 then
                    blam(entity)
                end
            end,
        },
    }
})
```

For high-entity-count games (such as bullet-heaven / horde-survival games with
thousands of simultaneous projectiles and enemies), you can also handle collision
batches and custom health/damage components natively in C++ via a
[Native C++ Game Module](#14-native-c-game-modules).

---

## 7. Scripting Entities

Attach a `script` component to give an entity per-frame Lua behaviour. The
engine calls `on_update(self, entity, dt)` every frame and `on_debug_gui(self,
entity)` when editor debug UI is visible. `self` is the script table — store
per-entity state there.

See [`docs/lua-scripting.md`](lua-scripting.md) for the full scripting guide:
update loop, `registry.get_*` / `registry.has_*`, all global helpers, input
binding, and ImGui debug UI.

---

## 8. Scenes

`load_scene(path)` replaces the current scene. Scene transitions are
synchronous — `load_scene` does not return to the caller.

```lua
load_scene("scenes/gameplay.lua")
reload_scene()      -- hot-reload the current scene (useful during development)
```

Switching scenes from inside a script:

```lua
script = {
    on_update = function(self, entity, dt)
        if input.is_key_pressed("r")      then load_scene("scenes/gameplay.lua") end
        if input.is_key_pressed("escape") then load_scene("scenes/title.lua")    end
    end,
}
```

Scene files can be declarative (return a table), procedural (return a function),
or side-effect style (build the scene at top level as the file executes). See
[`docs/scenes.md`](scenes.md) for the full lifecycle, file shape, and asset
preload flow.

---

## 9. Assets

Assets must be loaded before any entity references them. The recommended
approach is `acquire_scene_assets` — it accepts `preload` lists and scans
entity definitions for asset references automatically. Explicit `load_asset`
calls are also supported.

```lua
load_asset({ type = "texture", id = "player",    file = "images/player.png" })
load_asset({ type = "font",    id = "main-font", file = "fonts/arial.ttf", font_size = 16 })
load_asset({ type = "sound",   id = "jump-sfx",  file = "sounds/jump.wav" })
```

All asset paths are relative to the project root. Use `get_asset_path` when
building paths dynamically:

```lua
local path = get_asset_path("images/player.png")
```

For shipping builds, bake assets into a manifest with:

```bash
./OctarineEngine path/to/MyGame -m bake
```

See [`docs/asset-pipeline.md`](asset-pipeline.md) for `.meta` sidecars, atlas
packing, audio normalisation, and the full bake/manifest workflow.

---

## 10. Audio

```lua
play_sound("jump-sfx")          -- one-shot
play_sound("music-gameplay")    -- looped music
```

For spatial audio, add `audio_listener` to the player/camera entity and
`audio_source` to emitting entities. The engine handles attenuation
automatically.

```lua
audio_listener = {}

audio_source = {
    asset_id = "enemy-growl",
    loop     = true,
    volume   = 0.8,
}
```

---

## 11. Camera

The camera follows whichever entity carries a `camera_follow` component. Only
one should be active at a time.

```lua
camera_follow = {}
```

```lua
local cam = get_camera_position()            -- returns {x, y}
set_game_map_dimensions(width_px, height_px) -- clamp scrolling to world bounds
```

Entities with `fixed = true` on their `sprite`, `square`, or `text_label`
render in screen space, unaffected by camera movement — use this for HUD
elements.

---

## 12. Using the Editor

Launch an editor build against your project:

```bash
./OctarineEngine path/to/MyGame
```

The editor adds a toolbar, entity inspector, scene hierarchy, and your
`on_debug_gui` windows. See [`docs/editor.md`](editor.md) for the full panel
reference and keyboard shortcuts.

---

## 13. Game CI

The `setup-engine` composite action downloads the right SDK binary for the
current runner platform and adds it to `PATH`. Use it in game repo workflows to
bake assets or run the game headlessly without building the engine from source.

### Basic usage

```yaml
# .github/workflows/ci.yml (in your game repo)
jobs:
  ci:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - uses: Octarine-Club/octarine-engine/.github/actions/setup-engine@v0.1.0
        with:
          engine-version: '0.1.0'   # pin to the version in project.ini
          token: ${{ secrets.GITHUB_TOKEN }}

      # Bake the asset manifest (validates all asset references)
      - run: OctarineEngine . -m bake
```

### Pinning the engine version

Declare the engine version in your `project.ini` so it is visible alongside
your other identity metadata:

```ini
engine_version = 0.1.0
```

When you package the game (`cmake --preset ship-release ...`) the engine
compares this value against its own version at configure time and warns if
they differ, making version drift visible before it reaches CI.

### Available outputs

| Output | Description |
|---|---|
| `engine-path` | Directory containing the engine binary |
| `engine-bin` | Full path to the `OctarineEngine` binary |

### Multi-platform matrix

```yaml
strategy:
  matrix:
    os: [ubuntu-latest, windows-latest, macos-latest]
runs-on: ${{ matrix.os }}
steps:
  - uses: Octarine-Club/octarine-engine/.github/actions/setup-engine@v0.1.0
    with:
      engine-version: '0.1.0'
  - run: OctarineEngine . -m bake
```

---

## 14. Native C++ Game Modules

While most games can be written entirely in Lua, games with heavy simulation
workloads (e.g., thousands of agents with custom steering, spatial grids, or
projectile/combat pipelines) can compile game-specific C++ components, systems,
events, Lua bindings, unit tests, and benchmarks directly into the engine via
`OCTARINE_GAME_MODULE_DIR` — without adding any game-specific code to the engine
repository.

### 1. Directory Layout in Your Game Project

Place your C++ module in a subdirectory of your game repository (conventionally
`native/`):

```text
MyGame/
├── config.ini
├── project.ini
├── game.lua
└── native/
    ├── CMakeLists.txt              # Defines octarine_game_module
    ├── GameModule.cpp              # Implements game_module::* hooks
    ├── Components/
    │   └── HealthComponent.h
    ├── Systems/
    │   ├── CombatSystem.h
    │   └── CombatSystem.cpp
    ├── Events/
    │   └── DamageDealtEvent.h
    ├── Lua/Bindings/
    │   └── HealthComponentLuaBinding.h
    ├── Editor/Inspectors/          # Optional editor component inspectors
    │   └── HealthInspector.h
    ├── tests/                      # Optional CTest suite
    │   ├── CMakeLists.txt
    │   └── CombatSystemTest.cpp
    └── benchmarks/                 # Optional Google Benchmark suite
        ├── CMakeLists.txt
        └── SystemBenchmarks.cpp
```

### 2. Defining `octarine_game_module` (`native/CMakeLists.txt`)

When CMake is invoked with `-DOCTARINE_GAME_MODULE_DIR=/path/to/MyGame/native`,
the engine adds that directory to the build, defines `OCTARINE_HAS_GAME_MODULE`
on `octarine_core`, and links `octarine_game_module` into `octarine_engine`:

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

if (OCTARINE_WITH_EDITOR)
    target_link_libraries(octarine_game_module PUBLIC octarine_editor)
endif ()
```

### 3. Implementing the Game Module Hooks (`native/GameModule.cpp`)

Implement the six entry points declared in `Engine/GameModuleHooks.h`:

```cpp
// MyGame/native/GameModule.cpp
#include "Engine/GameModuleHooks.h"

#include "Components/HealthComponent.h"
#include "ECS/Registry.h"
#include "EventBus/EventBus.h"
#include "Lua/Bindings/ComponentBindingsRegistry.h"
#include "Lua/Bindings/HealthComponentLuaBinding.h"
#include "Lua/Bindings/LuaBindingContext.h"
#include "Systems/CombatSystem.h"

namespace game_module {

void RegisterComponentBindings() {
  ComponentBindingsRegistry::Instance().Register<HealthComponent>();
}

void RegisterComponentInspectors() {
#ifdef OCTARINE_WITH_EDITOR
  // Register custom ImGui component inspectors here if using OCTARINE_WITH_EDITOR
#endif
}

void InstallSingletons(Registry& registry) {
  // Set or initialize game-specific ECS singletons on the registry
  (void)registry;
}

void RegisterSimulationSystems(Registry& registry, const std::unique_ptr<EventBus>& eventBus,
                               SystemId velocityIntegration) {
  (void)eventBus;
  // Register per-frame simulation systems (Serial, Parallel, or Bulk).
  // Use `registry.Order(velocityIntegration).After(mySystem);` if a system
  // spawns entities or updates velocities before VelocityIntegrationSystem runs.
  (void)registry;
  (void)velocityIntegration;
}

void RegisterEventSystems(Registry& registry, const std::unique_ptr<EventBus>& eventBus) {
  // Register event-driven systems (e.g., subscribing to CollisionBatchEvent)
  registry.RegisterSystem<CombatSystem>(&registry, eventBus);
}

void InstallLuaFunctions(sol::state& lua, LuaBindingContext& ctx) {
  // Expose custom Lua globals or system helper functions
  (void)lua;
  (void)ctx;
}

}  // namespace game_module
```

### 4. Adding Game Module Unit Tests (`native/tests/CMakeLists.txt`)

When `OCTARINE_ENABLE_TESTS=ON` and `${OCTARINE_GAME_MODULE_DIR}/tests/CMakeLists.txt`
exists, the engine automatically includes it and creates an aggregate build target
named `octarine_game_module_tests`. You can use the engine's test helper macros
(`octarine_add_engine_test`, `octarine_add_core_test`, `octarine_add_test`) and
include `"TestHarness.h"`:

```cmake
# MyGame/native/tests/CMakeLists.txt
octarine_add_engine_test(OctarineCombatSystemTest
    CombatSystemTest.cpp
)
target_link_libraries(OctarineCombatSystemTest PRIVATE octarine_game_module)
add_test(NAME CombatSystemTest COMMAND OctarineCombatSystemTest)
```

### 5. Adding Game Module Benchmarks (`native/benchmarks/CMakeLists.txt`)

When `OCTARINE_ENABLE_BENCHMARKS=ON` and
`${OCTARINE_GAME_MODULE_DIR}/benchmarks/CMakeLists.txt` exists, the engine
includes it after defining `OctarineBenchmarks` so your game module can attach
custom `Sys/`, `Pipeline/`, or `Frame/` benchmarks:

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
# Configure and build the engine with your game's native module
cmake --preset player-debug \
  -DOCTARINE_GAME_MODULE_DIR=/path/to/MyGame/native \
  -DOCTARINE_ENABLE_TESTS=ON
cmake --build build/player-debug

# Run your game
./build/player-debug/bin/debug/OctarineEngine /path/to/MyGame

# Run all engine and game-module unit tests
ctest --test-dir build/player-debug --output-on-failure

# Or run the verification gate with your game module enabled
OCTARINE_GAME_MODULE_DIR=/path/to/MyGame/native scripts/octarine-verify.sh --fast
```

---

## 15. Further Reading

### Engine documentation

| Doc | What's in it |
|---|---|
| [`docs/ecs-components.md`](ecs-components.md) | Every component — all fields, types, and defaults |
| [`docs/lua-scripting.md`](lua-scripting.md) | Full Lua scripting guide |
| [`docs/scenes.md`](scenes.md) | Scene lifecycle, declarative vs. procedural forms |
| [`docs/asset-pipeline.md`](asset-pipeline.md) | `.meta` sidecars, baking, atlases, audio normalisation |
| [`docs/systems.md`](systems.md) | Built-in systems and the order they run |
| [`docs/events.md`](events.md) | The event bus — every event type and how to use them |
| [`docs/editor.md`](editor.md) | Editor panels, hotkeys, export workflow |
| [`docs/tilemaps.md`](tilemaps.md) | Tilemap scene field |
| [`docs/device-builds.md`](device-builds.md) | Shipping for desktop and Android |

### Lua API reference

[`lua_api.smoke.lua`](https://github.com/Octarine-Club/octarine-engine/blob/main/lua_api.smoke.lua)
is the authoritative, auto-generated EmmyLua stub of the entire Lua surface.
Load it into your editor (VS Code + the Lua Language Server extension is
recommended) for autocomplete and inline docs.

### Example project

[Octarine-Engine-Example](https://github.com/Octarine-Club/octarine-engine-example)
is a complete, runnable game showing every engine feature in practice.

### Engine repository

Source, issue tracker, and releases:
[github.com/Octarine-Club/octarine-engine](https://github.com/Octarine-Club/octarine-engine)

### Lua 5.4

The engine embeds Lua 5.4. Reference manual: [lua.org/manual/5.4](https://www.lua.org/manual/5.4/)

### Dear ImGui

The UI binding closely mirrors the C++ API. The
[ImGui wiki](https://github.com/ocornut/imgui/wiki) and header comments in
[`imgui.h`](https://github.com/ocornut/imgui/blob/master/imgui.h) are the best
references for functions not yet in the engine's Lua stub.
