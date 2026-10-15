#!/usr/bin/env bash
# Build and run the headless C++ benchmark suite (OctarineBenchmarks), write JSON, and print a
# markdown summary — or a diff against a baseline. No window, GPU, audio device, or game project
# is needed, so this is the entry point for agents verifying performance changes.
#
# Usage: scripts/bench-micro.sh [options]
#   --tier T          ECS | Sys | Pipeline | Frame | Misc | all   (default: all)
#   --filter REGEX    raw --benchmark_filter (overrides --tier)
#   --quick           short min-time, skips Frame/MaxEntities (default)
#   --full            longer min-time + 3 repetitions (median/cv), includes Frame/MaxEntities
#   --repetitions N   override repetition count
#   --baseline FILE   compare against a previous JSON (scripts/bench_compare.py)
#   --out FILE        JSON output path (default: build/bench-results/<timestamp>[_label].json)
#   --label NAME      suffix for the default output file name
#   --preset NAME     CMake preset/build dir to use (default: player-release)
#   --no-build        skip configure/build
#
# Examples:
#   scripts/bench-micro.sh --tier Sys --filter 'Sys/Separation'
#   scripts/bench-micro.sh --full --label before
#   scripts/bench-micro.sh --full --label after --baseline build/bench-results/<...>_before.json

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

tier="all"
filter=""
mode="quick"
repetitions=""
baseline=""
out=""
label=""
preset="player-release"
build=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tier) tier="$2"; shift 2 ;;
    --filter) filter="$2"; shift 2 ;;
    --quick) mode="quick"; shift ;;
    --full) mode="full"; shift ;;
    --repetitions) repetitions="$2"; shift 2 ;;
    --baseline) baseline="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    --label) label="$2"; shift 2 ;;
    --preset) preset="$2"; shift 2 ;;
    --no-build) build=0; shift ;;
    -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

build_dir="$repo_root/build/$preset"
if [[ $build -eq 1 ]]; then
  if [[ ! -f "$build_dir/CMakeCache.txt" ]] || ! grep -q "OCTARINE_ENABLE_BENCHMARKS:BOOL=ON" "$build_dir/CMakeCache.txt"; then
    cmake --preset "$preset" -DOCTARINE_ENABLE_BENCHMARKS=ON >&2
  fi
  cmake --build "$build_dir" --target OctarineBenchmarks >&2
fi

binary="$(find "$build_dir/bin" -type f -name OctarineBenchmarks -perm -u+x 2>/dev/null | head -n 1)"
if [[ -z "$binary" ]]; then
  echo "error: OctarineBenchmarks not found under $build_dir/bin" >&2
  exit 2
fi

if [[ -z "$filter" ]]; then
  case "$tier" in
    all) filter="^(ECS|Sys|Pipeline|Misc)/|^Frame/(Sim|SimSync|Churn|Lua)/" ;;
    Frame) filter="^Frame/(Sim|SimSync|Churn|Lua)/" ;;
    ECS|Sys|Pipeline|Misc) filter="^$tier/" ;;
    *) echo "unknown tier: $tier" >&2; exit 2 ;;
  esac
  if [[ "$mode" == "full" ]]; then
    case "$tier" in
      all) filter="^(ECS|Sys|Pipeline|Frame|Misc)/" ;;
      Frame) filter="^Frame/" ;;
    esac
  fi
fi

args=(--benchmark_filter="$filter" --benchmark_out_format=json)
if [[ "$mode" == "quick" ]]; then
  args+=(--benchmark_min_time=0.1s)
  [[ -n "$repetitions" ]] && args+=(--benchmark_repetitions="$repetitions" --benchmark_report_aggregates_only=true)
else
  args+=(--benchmark_min_time=0.5s --benchmark_repetitions="${repetitions:-3}" --benchmark_report_aggregates_only=true)
fi

if [[ -z "$out" ]]; then
  mkdir -p "$repo_root/build/bench-results"
  stamp="$(date +%Y%m%d_%H%M%S)"
  out="$repo_root/build/bench-results/${stamp}${label:+_$label}.json"
fi
args+=(--benchmark_out="$out")

echo "Running $binary (${mode}, filter: $filter)" >&2
# Console output to stderr so stdout carries only the markdown summary.
"$binary" "${args[@]}" >&2
echo "JSON: $out" >&2

if [[ -n "$baseline" ]]; then
  python3 "$script_dir/bench_compare.py" "$out" --baseline "$baseline"
else
  python3 "$script_dir/bench_compare.py" "$out"
fi
