# Quickstart

The shortest path from a fresh clone to a code change you can verify. For full C++ class/method reference, see the **[published Doxygen API docs](https://octarine-club.github.io/octarine-engine/)**.

## 1. Build

```bash
# Player build (ImGui debug overlays + Lua ImGui debug tooling, debug).
cmake --preset player-debug
cmake --build build/player-debug

# Optional: include a game project's native C++ module (components, systems, tests, benchmarks)
cmake --preset player-debug -DOCTARINE_GAME_MODULE_DIR=/path/to/game/native
cmake --build build/player-debug
```

CMake presets live in [`../CMakePresets.json`](../CMakePresets.json):

| Preset | When to use |
|---|---|
| `player-debug` | Day-to-day dev with ImGui debug overlays & Lua UI |
| `player-release` | Optimized dev runtime, no ImGui (live-scan catalog) |
| `player-profile` | RelWithDebInfo + `OCTARINE_ENABLE_PROFILING` for timing |
| `ship-release` | Canonical shipping config (manifest-load catalog, security hardening) |
| `ship-mac-universal` | Universal macOS shipping build (`arm64` + `x86_64`) |

To scaffold a new game project outside the engine tree:

```bash
./scripts/octarine-new.sh --name "My Game" --package-id com.example.mygame --dir ./my-game
```

Run `./scripts/octarine-init-build.sh` inside a game project directory (or with engine args) to configure build directories against a local engine checkout or installed SDK.

### Linux build prerequisites

vcpkg builds SDL3 (and its `alsa` dependency) **from source** on first configure, so the host needs the windowing/audio dev headers and autotools **before** that build runs. If they are absent, SDL3 silently disables the missing backends and the engine later exits with `SDL_Init Error: No available video device` (or `No available audio device`). Because vcpkg caches that build, installing packages afterward is not enough on its own.

On Debian/Ubuntu:

```bash
sudo apt install cmake ninja-build \
  autoconf autoconf-archive automake libtool \
  wayland-protocols libwayland-bin libxkbcommon-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
  libxfixes-dev libxtst-dev libxss-dev libxrender-dev \
  libegl1-mesa-dev libgles2-mesa-dev \
  libasound2-dev libpulse-dev libdbus-1-dev libdrm-dev libgbm-dev
```

If you already configured with missing packages, force a clean rebuild:

```bash
rm -rf build/<preset>/vcpkg_installed
VCPKG_BINARY_SOURCES=clear cmake --preset <preset>
```

To confirm the backends compiled in:

```bash
for s in wayland x11 alsa; do strings build/<preset>/bin/debug/OctarineEngine* | grep -qx "$s" && echo "present: $s"; done
```

## 2. Run

Pass the game project directory as a positional argument (or via `--project-root`):

```bash
# Run with dev overlays, live LuaLS stub generation, and hot reload
./build/player-debug/bin/debug/OctarineEngine ../Octarine-Engine-Example --dev
```

[`Octarine-Engine-Example`](https://github.com/Octarine-Club/octarine-engine-example) is the reference Lua-driven game project.

Bake-only mode (headless, no window) runs the asset pipeline, writes `asset_manifest.lua` (or encrypted `assets.ocpk` when packaged), and exits nonzero on unresolved references:

```bash
./build/player-debug/bin/debug/OctarineEngine ../Octarine-Engine-Example --startup-mode bake
```

See [`docs/dev-tools.md`](dev-tools.md) for `--headless`, `--scripts-dir`, `--use-manifest`, and `--dev-listen <port>`.

## 3. Verify

The test suite in [`../tests/CMakeLists.txt`](../tests/CMakeLists.txt) registers 38 CTest targets when `OCTARINE_ENABLE_TESTS=ON`:

```bash
cmake --preset player-debug -DOCTARINE_ENABLE_TESTS=ON
cmake --build build/player-debug
ctest --test-dir build/player-debug --output-on-failure
```

Key verification tools and tests:

- **`LuaApiSmokeTest`** — constructs `Game` headlessly, replays Lua binding setup, asserts every component/module/system surface is reachable, and regenerates [`../lua_api.smoke.lua`](../lua_api.smoke.lua).
- **`AssetPipelineTest`** — validates `.meta` sidecar parsing, catalog building, and manifest round-trips.
- **`./scripts/octarine-verify.sh --fast`** (`--full` for C++20 modules/sanitizers) — unified local verification gate.
- **`python3 scripts/check_architecture.py`** — checks include DAG layering and CMake target parity.
- **`python3 scripts/gen_api_catalogs.py --check`** — ensures generated catalogs stay synced with `src/`.

## 4. Where things live

| Path | What's there |
|---|---|
| `src/ECS/` | Archetype registry, 16 KB SoA chunks, queries, `CommandBuffer`, `StateInspector` |
| `src/Engine/` | `EngineBootstrap`, `SimulationSystems`, `FrameLoop`, `SceneLoader`, `GameModuleHooks.h` |
| `src/Game/` | `Game`, `GameConfig`, `EngineContext`, `UserSettings` |
| `src/Components/` | POD component structs (data only, descending-size packed) |
| `src/Systems/` | Simulation, collision, physics, audio, camera, scripting, render-collection systems |
| `src/UI/` | Declarative UI runtime, `.oxml` parser, `.oss` stylesheet engine, reactive observables |
| `src/Tilemap/` | Chunked LDtk tilemaps, IntGrid masks, 2D DDA raycasting, procedural stitching |
| `src/Lua/Bindings/` | `LuaBinding<T>` headers and 4 split `Register*Bindings.cpp` TUs |
| `src/Lua/Modules/` | `LuaModuleBinding<M>` free-function domain modules |
| `src/Renderer/` | SDL3 renderer, `RenderQueue`, `RenderKey`, atlas batching |
| `src/EventBus/`, `src/Events/` | Type-erased pub/sub event bus and event structs |
| `src/AssetManager/` | Asset catalog, `.meta` sidecars, manifest baking |
| `src/Crypto/` | OCPK ChaCha20+Poly1305 encryption and Lua bytecode protection |
| `src/Debug/`, `src/Dev/` | Dear ImGui debug panels, `DebugMenuRegistry`, `--dev-listen` loopback TCP server |
| `tests/` | Engine unit and integration test suites (`OCTARINE_ENABLE_TESTS=ON`) |
| `cmake/`, `scripts/` | Build modules, packaging, license aggregation, project scaffolding, verify scripts |
| `android/` | Gradle host app (multi-ABI, vcpkg Android triplets) |
| `components.json`, `modules.json`, `systems.json`, `events.json`, `lua_api.smoke.lua` | Auto-generated API catalogs and EmmyLua stub |

## 5. Adding new code

> [!NOTE]
> Game-specific components, systems, events, Lua bindings, tests, and benchmarks belong in the game project's native module (`-DOCTARINE_GAME_MODULE_DIR=/path/to/game/native`) rather than inside the engine repository. See [Native C++ Game Modules](making-games.md#14-native-c-game-modules) and [`docs/extension-recipes.md`](extension-recipes.md).

| To add to the engine | Touch points |
|---|---|
| A **component** | `src/Components/X.h` + `src/Lua/Bindings/XLuaBinding.h` + `LuaComponentRegistry::registerComponent<X>()` in the matching `src/Lua/Bindings/Register*Bindings.cpp` TU (`TransformCore`, `Render`, `PhysicsAudioScript`, or `UI`) + regenerate catalogs (`LuaApiSmokeTest` + `python3 scripts/gen_api_catalogs.py`). |
| A **Lua module** | `src/Lua/Modules/XModuleLuaBinding.{h,cpp}` + `kLuaModules` in `src/Lua/Modules/RegisterAllModules.cpp` + add `.cpp` to `octarine_lua` in `src/CMakeLists.txt` + regenerate catalogs. |
| A **system** | `src/Systems/X.{h,cpp}` + add `.cpp` to `octarine_systems` in `src/CMakeLists.txt` + register in `src/Engine/SimulationSystems.cpp` (`registry.Order(...).After(...)`) or `Game::Setup` + regenerate catalogs (`python3 scripts/gen_api_catalogs.py`). |
| An **event** | `src/Events/X.h` + `EmitEvent<X>(...)` + `[[nodiscard]] SubscriptionHandle SubscribeEvent<TOwner, X>(...)` retained by the consumer + `python3 scripts/gen_api_catalogs.py`. |
| A **new `.cpp`** under `src/` | Register it in the matching layer list in `src/CMakeLists.txt` (`octarine_core`, `octarine_assets`, `octarine_renderer`, `octarine_systems`, `octarine_lua`, `octarine_engine`). |

## Deeper reading

- [`docs/making-games.md`](making-games.md) — end-to-end game creation, project config, assets, Lua, and native C++ modules
- [`docs/extension-recipes.md`](extension-recipes.md) — step-by-step extension checklists for ECS, Lua, events, and native modules
- [`docs/ecs-architecture.md`](ecs-architecture.md) — archetype graph, 16 KB SoA chunk storage, queries, and DAG ordering
- [`docs/ecs-components.md`](ecs-components.md) — spawn schema and all 39 registered components
- [`docs/systems.md`](systems.md) — built-in systems and DAG execution order
- [`docs/events.md`](events.md) — the `EventBus` and built-in event types
- [`docs/lua-scripting.md`](lua-scripting.md) — scripts, callbacks, component proxies, globals, `storage`, and `settings`
- [`docs/scenes.md`](scenes.md) — scene file schema, lifecycle, `load_scene`, and `reload_scene`
- [`docs/input-and-controllers.md`](input-and-controllers.md) — action mapping, canonical gamepad controls, and UI focus navigation
- [`docs/ui-framework.md`](ui-framework.md) — declarative `.oxml` templates, `.oss` stylesheets, and reactive MVVM bindings
- [`docs/ui-layout.md`](ui-layout.md) — canvas scaling, anchor positioning, flexbox, clipping, nine-slice, and scroll views
- [`docs/tilemaps.md`](tilemaps.md) — chunked LDtk tilemaps, IntGrid spatial queries, 2D DDA raycasting, stitching, and prop streaming
- [`docs/dev-tools.md`](dev-tools.md) — `--dev`, `dev.*` panels, LuaLS stubs, `--headless`, frame capture, and `--dev-listen`
- [`docs/asset-pipeline.md`](asset-pipeline.md) — `.meta` sidecars, bake step, atlases, and audio normalization
- [`docs/profiling.md`](profiling.md) — profiling build, PerfUtils, `OctarineBenchmarks`, and benchmark dashboard
- [`docs/security.md`](security.md) — OCPK bundle encryption, Lua bytecode protection, and filesystem sandboxing
- [`docs/device-builds.md`](device-builds.md) — shipping artifacts for desktop (and experimental Android/iOS)
- [`docs/commenting-guide.md`](commenting-guide.md) — code commenting standards
- [`lua_api.smoke.lua`](../lua_api.smoke.lua) — auto-generated EmmyLua reference for the live Lua surface
