# Octarine Engine

![Octarine Engine](https://img.shields.io/badge/Aesthetic-Yellowish--Purple-9370DB?style=for-the-badge)
![C++20](https://img.shields.io/badge/C++-20-blue.svg?style=for-the-badge&logo=c%2B%2B)
![Lua](https://img.shields.io/badge/Lua-5.4-blue.svg?style=for-the-badge&logo=lua)

Octarine Engine is a lightweight, high-performance 2D game engine built with C++20. It features a custom archetype-based Entity-Component System (ECS), integrated Lua scripting, and a unique yellowish-purple aesthetic.

New here? Start with [`docs/QUICKSTART.md`](docs/QUICKSTART.md) — the shortest path from a fresh clone to a verified code change.

## Tech Stack

- **Graphics:** SDL3
- **Scripting:** Lua 5.4 with [sol2](https://github.com/ThePhD/sol2) bindings
- **ECS:** Custom Archetype-based implementation
- **Math:** [GLM](https://github.com/g-truc/glm)
- **UI:** [ImGui](https://github.com/ocornut/imgui) with custom sol2 bindings
- **Logging:** [spdlog](https://github.com/gabime/spdlog)
- **Dependency Management:** [vcpkg](https://github.com/microsoft/vcpkg)

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
- **Editor [Experimental]:** The C++ editor (`OCTARINE_WITH_EDITOR`) is currently experimental and disabled by default. Active game development uses Lua-based ImGui debug tools (`onDebugGUI`) instead.

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

The compiled binaries will be located in `build/[preset-name]/bin/`.

For producing shippable artifacts (Windows ZIP/NSIS, Linux TGZ, macOS DMG, Android APK/AAB)
see [`docs/device-builds.md`](docs/device-builds.md). Shipping config is the `ship-release`
preset (and `ship-mac-universal` for universal macOS).

Contributing? See [`CONTRIBUTING.md`](CONTRIBUTING.md) for workflow, coding
standards, commit style, and how to register new source files.

### Running the Engine

Octarine Engine accepts the game directory as a positional argument:

```bash
# Run a game project
./OctarineEngine ./projects/my-game

# Run with a specific startup mode (e.g. for benchmarking)
./OctarineEngine ./projects/my-game --startup-mode stress
```

---

## Benchmarking

### Continuous Benchmarking

The engine runs automated performance benchmarks via GitHub Actions on every push to `main` and Pull Request.

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
