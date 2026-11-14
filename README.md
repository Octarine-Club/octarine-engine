<p align="center">
  <img src="docs/assets/logo-128.png" alt="Octarine Engine Logo" width="128" height="128" />
</p>

# Octarine Engine

![Octarine Engine](https://img.shields.io/badge/Aesthetic-Yellowish--Purple-9370DB?style=for-the-badge)
![C++20](https://img.shields.io/badge/C++-20-blue.svg?style=for-the-badge&logo=c%2B%2B)
![Lua](https://img.shields.io/badge/Lua-5.4-blue.svg?style=for-the-badge&logo=lua)

Octarine Engine is a lightweight, high-performance 2D game engine built with C++20. It features a custom archetype-based Entity-Component System (ECS), integrated Lua 5.4 scripting (`sol2`), SDL3 multimedia backends, a declarative UI framework (`.oxml`/`.oss`), and chunked LDtk tilemaps.

- **New here?** Start with [`docs/QUICKSTART.md`](docs/QUICKSTART.md) (building the engine) or [`docs/making-games.md`](docs/making-games.md) (building a game).
- **C++ API Reference:** Published Doxygen documentation at **[octarine-club.github.io/octarine-engine](https://octarine-club.github.io/octarine-engine/)**.

### Documentation

- [Making Games](docs/making-games.md) — project structure, `config.ini`, assets, Lua scripting, and native C++ modules
- [Quickstart](docs/QUICKSTART.md) — clone, build, test, and verify engine changes
- [Lua Scripting](docs/lua-scripting.md) — script callbacks, component proxies, globals, `storage`, and `settings`
- [ECS Architecture](docs/ecs-architecture.md) and [ECS Components](docs/ecs-components.md) — 16 KB SoA chunks, DAG scheduling, and all 39 components
- [Systems](docs/systems.md) and [Events](docs/events.md) — execution order, collision shapes, and `EventBus`
- [Scenes](docs/scenes.md) and [Input & Controllers](docs/input-and-controllers.md) — scene switching, action bindings, and gamepad support
- [UI Framework](docs/ui-framework.md) and [UI Layout](docs/ui-layout.md) — declarative `.oxml`/`.oss`, observables, and procedural flex/anchors
- [Tilemaps](docs/tilemaps.md) — chunked LDtk maps, DDA raycasting, IntGrid masks, and procedural stitching
- [Dev Tools](docs/dev-tools.md) — `--dev`, `dev.*` ImGui panels, LuaLS stubs, `--headless`, and `--dev-listen`
- [Asset Pipeline](docs/asset-pipeline.md), [Profiling](docs/profiling.md), [Security](docs/security.md), and [Device Builds](docs/device-builds.md)

## Tech Stack

- **Multimedia:** SDL3 (`SDL3`, `SDL3_image`, `SDL3_ttf`, `SDL3_mixer`)
- **Scripting:** Lua 5.4 with [sol2](https://github.com/ThePhD/sol2) bindings
- **ECS:** Custom 16 KB SoA archetype implementation with DAG scheduling
- **Math:** [GLM](https://github.com/g-truc/glm)
- **Debug UI:** [Dear ImGui](https://github.com/ocornut/imgui) with custom `sol2` bindings (`--dev` / non-shipped builds)
- **Logging:** [spdlog](https://github.com/gabime/spdlog)
- **Dependency Management:** [vcpkg](https://github.com/microsoft/vcpkg) (manifest mode)

---

## Third-Party Licenses

Octarine Engine itself is MIT-licensed (see [`LICENSE`](LICENSE)). Every shipped package
(desktop ZIP/TGZ/DMG, Android AAB) ships a build-time-aggregated
`THIRD_PARTY_LICENSES.txt` covering every linked dependency. Aggregation lives in
[`cmake/octarine-licenses.cmake`](cmake/octarine-licenses.cmake).

Portions of this software are copyright © The FreeType Project (www.freetype.org). All rights
reserved. FreeType is used under the terms of the FreeType License (FTL); see the bundled
`THIRD_PARTY_LICENSES.txt` for the full text and for every other linked dependency.

---

## Prerequisites

- **CMake:** Version 3.15+
- **C++ Compiler:** C++20 compliant (GCC 11+, Clang 13+, MSVC 2022+)
- **vcpkg:** For managing dependencies (manifest mode — all deps, including the
  sol2 ImGui bindings, are fetched and built by vcpkg on first configure).
- **Git:** To clone the repository.

---

## Building the Engine

The engine uses **CMake Presets** to manage different build configurations. The primary development variants are:

- **Player (Debug with UI):** Day-to-day development with Dear ImGui debug overlays and Lua ImGui bindings (`player-debug`).
- **Player (Dev, live-scan):** Minimal, optimized dev player runtime without ImGui (`player-release`).
- **Profile:** Optimized player with performance instrumentation (`spdlog` timers) enabled (`player-profile`).
- **In-Game Debug Overlays:** Development builds include Dear ImGui and Lua-based debug tooling (`on_debug_gui`) for inspection and profiling. Compiled out on shipping builds.
- **Mobile Shipping (Android / iOS) [Experimental]:** Shipping build support for Android and iOS is experimental and deferred into the future. Current engine development and production shipping workflows target desktop platforms (Windows, Linux, macOS).

### Build Commands

```bash
# Clone the repository
git clone https://github.com/Octarine-Club/octarine-engine.git
cd octarine-engine

# List all available presets
cmake --list-presets

# Build the Player with ImGui debug tooling (Standard development)
cmake --preset player-debug
cmake --build --preset player-debug

# Build the Player (optimized dev runtime, live-scan catalog)
cmake --preset player-release
cmake --build --preset player-release

# Build the Profiling Player (For performance analysis)
cmake --preset player-profile
cmake --build --preset player-profile
```

The compiled binaries will be located in `build/<preset>/bin/<config>/` (for example, `build/player-debug/bin/debug/OctarineEngine`).

To scaffold a standalone game project outside the engine tree:

```bash
./scripts/octarine-new.sh --name "My Game" --package-id com.example.mygame --dir ./my-game
# Optional helper when setting up a local engine or native-module build:
./scripts/octarine-init-build.sh
```

Run the verification suite after engine changes:

```bash
./scripts/octarine-verify.sh --fast
ctest --test-dir build/player-debug --output-on-failure
```

For producing shippable desktop artifacts (Windows ZIP/NSIS, Linux TGZ, macOS DMG; Android and iOS shipping support is experimental),
see [`docs/device-builds.md`](docs/device-builds.md). Shipping presets are `ship-release`
(and `ship-mac-universal` for universal macOS).

Contributing? See [`CONTRIBUTING.md`](CONTRIBUTING.md) for workflow, coding
standards, commit style, and how to register new source files.

### Running the Engine

Octarine Engine accepts the game directory as a positional argument or via `--project-root`:

```bash
# Run a game project
./build/player-debug/bin/debug/OctarineEngine ./projects/my-game

# Run with dev overlays, LuaLS type-stub refresh, and hot reload
./build/player-debug/bin/debug/OctarineEngine ./projects/my-game --dev

# Run headlessly, with a custom startup mode, or bake assets
./build/player-debug/bin/debug/OctarineEngine ./projects/my-game --headless --startup-mode stress
./build/player-debug/bin/debug/OctarineEngine ./projects/my-game --startup-mode bake
```

See [`docs/dev-tools.md`](docs/dev-tools.md) for `--scripts-dir`, `--use-manifest`, and `--dev-listen <port>`.

---

## Benchmarking

### C++ Microbenchmarks and Macro Runs

The primary benchmark suite is the headless C++ benchmark runner (`OctarineBenchmarks`):

```bash
./scripts/bench-micro.sh
```

For whole-game macro runs against a game directory:

```bash
# Run against octarine-engine-example using player-profile
scripts/bench.sh

# Run against a specific game directory
OCT_BENCH_GAME=/path/to/game scripts/bench.sh
```

### Continuous Benchmarking

The `.github/workflows/benchmark.yml` workflow runs on self-hosted runners via manual dispatch (`workflow_dispatch`).

Interactive historical performance dashboard:
**[Octarine Engine Performance Dashboard](https://octarine-club.github.io/octarine-engine/dev/bench/)**

### Local Profiling

The `player-profile` preset is optimized for performance analysis. `scripts/bench.sh` uses this build to stream timing data:

```bash
# Run for 8s using player-profile preset against octarine-engine-example.
scripts/bench.sh

# Run against a specific game directory
OCT_BENCH_GAME=/path/to/game scripts/bench.sh
```

For ad-hoc profiling runs:

```bash
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build/player-profile/bin/relwithdebinfo/OctarineEngine /path/to/game --startup-mode bench
```

## Compiler Caching (`ccache`)

Octarine automatically detects and enables [`ccache`](https://ccache.dev/) or `sccache` when available on your `PATH` via `OCTARINE_USE_CCACHE=ON`, accelerating both clean and incremental builds.

To install it:
- **macOS:** `brew install ccache`
- **Linux:** `sudo apt-get install ccache` (or distribution equivalent)
- **Windows:** `winget install ccache` / `choco install ccache`

For installation instructions and configuration options, see the [official ccache website](https://ccache.dev/).

