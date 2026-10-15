#!/usr/bin/env bash
# Run OctarineEngine headlessly for a short period to capture TIMER: output.
#
# Usage: scripts/bench.sh [duration_seconds]
#   duration: integer seconds (default: 8)
#
# Always uses the player-profile preset — that's the only build that emits
# TIMER lines. The stress scene reaches steady state in ~2s. The default 8s run
# combined with the parser's --warmup 2 flag gives ~6s of pure plateau data.
#
# This is the macro (whole-game, Lua + renderer, wall-clock) benchmark. For deterministic,
# headless C++ benchmarks of the ECS and systems use scripts/bench-micro.sh instead.
#
# Env:
#   OCT_BENCH_GAME   Path to a game dir containing config.ini.
#                    Default: ~/workspace/gh/dash-vs if present, else
#                    <repo>/../Octarine-Engine-Example (auto-cloned when missing).
#   OCT_BENCH_MODE   Startup mode passed to the game's Lua (selects which scene
#                    to auto-load). Default: benchmark_10k for dash-vs, stress for
#                    the example game (which also accepts main, map_editor, overlap).
#
# Exits 0 on the expected `timeout` kill (124) or a clean exit (0). Anything
# else (build failure, missing config, segfault) propagates.

set -euo pipefail

duration="${1:-8}"

preset=player-profile
config_dir=relwithdebinfo

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
build_dir="$repo_root/build/$preset"
binary="$build_dir/bin/$config_dir/OctarineEngine"
if [[ ! -x "$binary" && -x "$build_dir/bin/$config_dir/OctarineEngine-player" ]]; then
  binary="$build_dir/bin/$config_dir/OctarineEngine-player"
fi

default_game="$repo_root/../Octarine-Engine-Example"
default_mode="stress"
if [[ -d "$HOME/workspace/gh/dash-vs" ]]; then
  default_game="$HOME/workspace/gh/dash-vs"
  default_mode="benchmark_10k"
fi
game_path="${OCT_BENCH_GAME:-$default_game}"
if [[ ! -d "$game_path" ]]; then
  echo "Notice: Example game not found at $game_path." >&2
  echo "Auto-cloning Octarine-Engine-Example for benchmarking..." >&2
  
  auto_clone_dir="$repo_root/.benchmark_game"
  if [[ ! -d "$auto_clone_dir" ]]; then
    git clone --depth 1 https://github.com/Octarine-Club/octarine-engine-example.git "$auto_clone_dir" >&2
  else
    # Ensure it's up to date if it already exists
    git -C "$auto_clone_dir" pull >&2
  fi
  game_path="$auto_clone_dir"
fi
game_path="$(cd "$game_path" && pwd)"
if [[ ! -f "$game_path/config.ini" ]]; then
  echo "error: $game_path is missing config.ini" >&2
  exit 2
fi

if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
  cmake --preset "$preset" >&2
fi
if [[ ! -x "$binary" ]]; then
  cmake --build "$build_dir" --target OctarineEngine -- -k 0 >&2
fi

mode="${OCT_BENCH_MODE:-$default_mode}"

set +e
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  perl -e 'alarm shift; exec @ARGV' "$duration" "$binary" "$game_path" --startup-mode "$mode"
status=$?
set -e

if [[ $status -eq 124 || $status -eq 142 || $status -eq 0 ]]; then
  exit 0
fi
exit "$status"
