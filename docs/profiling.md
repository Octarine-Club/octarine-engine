# Profiling and benchmarks

The engine has three layers of performance tooling: compile-time scoped timers you can sprinkle in
code, a macro-benchmark harness that runs the real engine headless, and micro-benchmarks for hot data
structures. CI tracks the latter two nightly.

## The `player-profile` preset

Profiling instrumentation is compiled out by default (zero overhead). Turn it on with the
`player-profile` preset, which builds `RelWithDebInfo` with `OCTARINE_ENABLE_PROFILING=ON`:

```bash
cmake --preset player-profile
cmake --build build/player-profile
./build/player-profile/bin/relwithdebinfo/OctarineEngine ../Octarine-Engine-Example
```

`OCTARINE_ENABLE_PROFILING` defines `OCTARINE_PROFILING`, which activates the `PerfUtils` macros. With it off, every macro below expands to `((void)0)`. Use `--headless` for reproducible headless runs without a GPU/window, or `--dev` to inspect live metrics in-game via `RenderDebugGUISystem`.

## In-code timers — `src/General/PerfUtils.h`

| Macro | Use |
|-------|-----|
| `PROFILE_SCOPE` | Time the enclosing scope, labeled by function name. Emits a `TIMER:` line. |
| `PROFILE_NAMED_SCOPE(name)` | Same, with an explicit label. |
| `ACCUMULATE_PROFILE_SCOPE(name)` | Accumulate time across many calls under one label (per-frame totals). |
| `PROFILE_COUNTER_ADD/SET/INC(name, …)` | Numeric counters (e.g. chunks processed). |
| `PROFILE_COUNTERS_REPORT()` | Dump accumulated counters. |

`Registry::Update` already wraps each system in `ACCUMULATE_PROFILE_SCOPE(<system name>)`, so a
profiling build gives you per-system frame costs for free. In ImGui builds, `RenderDebugGUISystem`
surfaces these in an on-screen profiler window.

## Macro-benchmarks — `scripts/bench.sh`

Runs the actual engine headless for a few seconds and captures the `TIMER:` output:

```bash
scripts/bench.sh [duration_seconds]   # default 8s
```

It builds the `player-profile` preset if needed, runs with `SDL_VIDEODRIVER=dummy` /
`SDL_AUDIODRIVER=dummy` and `--startup-mode stress`, then formats the timer stream via
`scripts/parse_bench_output.py`. Override the game dir with `OCT_BENCH_GAME` and the scene with
`OCT_BENCH_MODE` (default `stress`; the example game also accepts `main`, `map_editor`, `overlap`).

## Micro-benchmarks — `scripts/bench-micro.sh` & Google Benchmark

Targeted headless C++ benchmarks for hot paths (ECS, systems, pipeline stages, frame loops, and misc) live in `tests/benchmarks/` and build into `OctarineBenchmarks` when `OCTARINE_ENABLE_BENCHMARKS=ON`. Use `scripts/bench-micro.sh` as the primary entry point (no window, GPU, or game project required):

```bash
# Quick microbenchmark run (default preset: player-release)
scripts/bench-micro.sh --tier Sys --quick

# Full run with 3 repetitions + PMU hardware counters, saved as a baseline
scripts/bench-micro.sh --full --label before

# Compare after changes against the baseline via scripts/bench_compare.py
scripts/bench-micro.sh --full --label after --baseline build/bench-results/<timestamp>_before.json
```

Or invoke CMake / `OctarineBenchmarks` directly:

```bash
cmake --preset player-profile -DOCTARINE_ENABLE_BENCHMARKS=ON
cmake --build build/player-profile --target OctarineBenchmarks
./build/player-profile/bin/relwithdebinfo/OctarineBenchmarks --benchmark_format=json
```

## CI and the dashboard

`.github/workflows/benchmark.yml` runs on manual `workflow_dispatch` on a self-hosted runner (for stable numbers; the nightly schedule is currently disabled). It runs both the micro and macro suites and publishes results via `benchmark-action/github-action-benchmark` to GitHub Pages (under `dev/bench/`) so you can track trends over time.

Regressions past the **200%** alert threshold are flagged but **do not fail** the run — the dashboard
and alert are advisory. Benchmarks are intentionally not part of the per-PR gate (they need the
dedicated runner); check the dashboard after a perf-sensitive change rather than relying on CI to
block one.
