#!/usr/bin/env bash
# One-command change validator. Runs the same gates CI runs, in dependency order, and prints an
# unambiguous final verdict. Run this after any change to ensure nothing broke.
#
# Usage: scripts/octarine-verify.sh [--fast|--full] [options]
#   --fast           (default) configure-if-needed -> build test targets -> ctest -> clang-format check
#   --full           also: build the engine, offline headless bake smoke, Lua-API drift check, clang-tidy
#   --tests <regex>  filter ctest execution to tests matching <regex> (passed as ctest -R <regex>)
#   --keep-going     run every gate and report a summary instead of stopping at the first failure
#   --no-build       skip configure/build (reuse the existing build dir as-is)
#   --format-only    run only the clang-format check on changed files
#   --tidy-strict    treat clang-tidy findings as a failure (default: advisory/non-blocking)
#   --json           emit machine-readable JSON verdict
#   -h, --help       show this help
#
# Pinned to the player-debug preset + OCTARINE_ENABLE_TESTS=ON so local green == CI green
# (.github/workflows/build.yml validates exactly this leg).
#
# Exit codes (distinct, so callers can branch):
#   0  all gates passed            13  bake smoke failed
#   2  environment/setup error     14  Lua-API artifact drift
#   10 configure failed            15  clang-format violations
#   11 build failed                16  system/event catalog drift or clang-tidy
#   12 ctest failed                17  architecture boundary violation
#
# Env:
#   OCT_VERIFY_GAME  Optional path to a game dir (with config.ini) for the bake smoke.
#                    Defaults to an isolated copy of tests/fixtures/bake-project under build/
#                    so --full runs 100% offline with zero network dependency.

set -uo pipefail

# CI pinned tool versions (.github/workflows/build.yml & nightly-tidy.yml)
CI_CLANG_FORMAT_VERSION="18.1.8"
CI_CLANG_TIDY_MAJOR="18"

preset=player-debug
config_dir=debug

mode=fast
keep_going=0
do_build=1
format_only=0
tidy_strict=0
json_output=0
test_regex=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --fast) mode=fast ;;
    --full) mode=full ;;
    --tests)
      if [[ $# -lt 2 || -z "${2:-}" ]]; then
        echo "error: --tests requires a non-empty <regex> argument" >&2
        exit 2
      fi
      test_regex="$2"
      shift
      ;;
    --tests=*)
      test_regex="${1#--tests=}"
      if [[ -z "$test_regex" ]]; then
        echo "error: --tests requires a non-empty <regex> argument" >&2
        exit 2
      fi
      ;;
    --keep-going) keep_going=1 ;;
    --no-build) do_build=0 ;;
    --format-only) format_only=1 ;;
    --tidy-strict) tidy_strict=1 ;;
    --json) json_output=1 ;;
    -h|--help) sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "error: unknown argument '$1' (try --help)" >&2; exit 2 ;;
  esac
  shift
done

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
cd "$repo_root"
build_dir="$repo_root/build/$preset"
binary="$build_dir/bin/$config_dir/OctarineEngine"

# Distinct exit code recorded by the first failing gate (or the worst, under --keep-going).
first_fail_code=0
first_fail_name=""

fail() {  # fail <code> <name>
  local code="$1" name="$2"
  echo "  -> FAIL: $name [$code]" >&2
  if [[ $first_fail_code -eq 0 ]]; then
    first_fail_code="$code"
    first_fail_name="$name"
  fi
  if [[ $keep_going -eq 0 ]]; then
    verdict
    exit "$first_fail_code"
  fi
}

verdict() {
  if [[ $json_output -eq 1 ]]; then
    if [[ $first_fail_code -eq 0 ]]; then
      printf '{"status": "passed", "exit_code": 0, "mode": "%s"}\n' "$mode"
    else
      printf '{"status": "failed", "exit_code": %d, "stage": "%s", "mode": "%s"}\n' "$first_fail_code" "$first_fail_name" "$mode"
    fi
    return
  fi
  if [[ $first_fail_code -eq 0 ]]; then
    echo "VERIFY OK ($mode)"
  else
    echo "VERIFY FAIL: $first_fail_name [$first_fail_code]" >&2
  fi
}

resolve_clang_tidy() {
  if command -v "clang-tidy-$CI_CLANG_TIDY_MAJOR" >/dev/null 2>&1; then
    command -v "clang-tidy-$CI_CLANG_TIDY_MAJOR"
  elif command -v clang-tidy >/dev/null 2>&1; then
    command -v clang-tidy
  fi
}

warn_clang_format_drift() {
  local fmt="$1"
  local ver_out ver
  ver_out="$("$fmt" --version 2>/dev/null || true)"
  ver="$(printf '%s\n' "$ver_out" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 || true)"
  if [[ -z "$ver" || "$ver" != "$CI_CLANG_FORMAT_VERSION" ]]; then
    echo "  -> WARN: clang-format version '${ver:-unknown}' differs from CI pin '$CI_CLANG_FORMAT_VERSION' (pipx install clang-format==$CI_CLANG_FORMAT_VERSION)" >&2
  fi
}

warn_clang_tidy_drift() {
  local tidy="$1"
  local ver_out major
  ver_out="$("$tidy" --version 2>/dev/null || true)"
  major="$(printf '%s\n' "$ver_out" | grep -oE 'version [0-9]+\.[0-9]+' | head -1 | grep -oE '[0-9]+' | head -1 || true)"
  if [[ -z "$major" || "$major" != "$CI_CLANG_TIDY_MAJOR" ]]; then
    echo "  -> WARN: clang-tidy major version '${major:-unknown}' differs from CI pin 'clang-tidy-$CI_CLANG_TIDY_MAJOR'" >&2
  fi
}

check_tool_versions() {
  local fmt tidy
  fmt="$(command -v clang-format || true)"
  [[ -n "$fmt" ]] && warn_clang_format_drift "$fmt"
  tidy="$(resolve_clang_tidy || true)"
  [[ -n "$tidy" ]] && warn_clang_tidy_drift "$tidy"
}

# List changed C/C++ files under src/, tests/, and examples/ (added/copied/modified/renamed vs HEAD).
# Falls back to scanning src/ + tests/ + examples/ outside a git context.
changed_sources() {
  if git rev-parse --git-dir >/dev/null 2>&1; then
    {
      git diff --name-only --diff-filter=ACMR HEAD -- 'src/*' 'tests/*' 'examples/*'
      git ls-files --others --exclude-standard -- 'src/*' 'tests/*' 'examples/*'
    } | sort -u | grep -E '\.(cpp|h|hpp)$' || true
  else
    find src tests examples -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) 2>/dev/null || true
  fi
}

check_format() {
  local fmt; fmt="$(command -v clang-format || true)"
  if [[ -z "$fmt" ]]; then
    echo "  -> SKIP clang-format (not on PATH)" >&2
    return 0
  fi
  warn_clang_format_drift "$fmt"
  local files=()
  while IFS= read -r line; do
    [[ -n "$line" ]] && files+=("$line")
  done < <(changed_sources)
  if [[ ${#files[@]} -eq 0 ]]; then
    echo "  ok   clang-format (no changed sources)"
    return 0
  fi
  if "$fmt" --dry-run --Werror -style=file "${files[@]}"; then
    echo "  ok   clang-format (${#files[@]} files)"
  else
    echo "  (fix with: cmake --build $build_dir --target format)" >&2
    fail 15 "clang-format"
  fi
}

check_tidy() {
  local tidy; tidy="$(resolve_clang_tidy || true)"
  if [[ -z "$tidy" ]]; then
    echo "  -> SKIP clang-tidy (not on PATH)" >&2
    return 0
  fi
  warn_clang_tidy_drift "$tidy"
  local tidy_db_dir="$build_dir"
  if [[ -f "$repo_root/build/lint/compile_commands.json" ]]; then
    tidy_db_dir="$repo_root/build/lint"
  fi
  if [[ ! -f "$tidy_db_dir/compile_commands.json" ]]; then
    echo "  -> SKIP clang-tidy (no compile_commands.json - configure first)" >&2
    return 0
  fi
  local files=()
  while IFS= read -r line; do
    [[ -n "$line" ]] && files+=("$line")
  done < <(changed_sources | grep -E '\.cpp$' || true)
  if [[ ${#files[@]} -eq 0 ]]; then
    echo "  ok   clang-tidy (no changed .cpp files)"
    return 0
  fi
  if "$tidy" -p "$tidy_db_dir" "${files[@]}"; then
    echo "  ok   clang-tidy (${#files[@]} files)"
  elif [[ $tidy_strict -eq 1 ]]; then
    fail 16 "clang-tidy"
  else
    echo "  -> clang-tidy reported findings (advisory; pass --tidy-strict to gate)" >&2
  fi
}

# --- format-only fast exit ---------------------------------------------------
if [[ $format_only -eq 1 ]]; then
  echo "== clang-format (changed files) =="
  check_format
  verdict
  exit "$first_fail_code"
fi

check_tool_versions

# --- configure + build -------------------------------------------------------
test_targets=(
  OctarineLuaApiTest OctarineAssetPipelineTest OctarineProjectIniTest
  OctarineEcsTest OctarineEcsHierarchyTest OctarineSystemLogicTest OctarineEventBusTest OctarineCryptoTest
  OctarineKDTreeCollisionSystemTest OctarineCollisionResponseTest
  OctarineStateInspectorTest OctarineGoldenPathIntegrationTest OctarineSimulationFuzzTest OctarineScenarioRunnerTest
  OctarineAssetRefcounterTest OctarineDevListenServerTest OctarineLuaBehaviorTest
  OctarineFileWatcherTest OctarineGameBakeTest OctarineFileSandboxTest
  OctarineRenderQueueTest OctarineTilemapSystemTest OctarineAssetStoreTest OctarineUserSettingsTest OctarineUILayoutTest
  OctarineCommandBufferTest OctarineEcsPropertyFuzzTest OctarineGameplaySystemsTest OctarineAudioSystemsTest
  OctarineInputSystemTest OctarineLuaModulesTest OctarineLuaTableSerializerTest OctarineParserRobustnessTest
  OctarineHotReloadTest OctarineLayoutInvariantsTest OctarineZeroAllocFrameTest OctarineRenderQueueStatsTest
  OctarineDebugMenuRegistryTest OctarineDebugThemeTest OctarineEngineIconTest
  OctarineShippedPakSmokeTest OctarineSceneSwapStressTest
  octarine_game_module_tests
)

if [[ $do_build -eq 1 ]]; then
  if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
    echo "== configure ($preset) =="
    cmake_args=(--preset "$preset" -DOCTARINE_ENABLE_TESTS=ON -DOCTARINE_WARNINGS_AS_ERRORS=ON)
    if [[ -n "${OCTARINE_GAME_MODULE_DIR:-}" ]]; then
      cmake_args+=(-DOCTARINE_GAME_MODULE_DIR="$OCTARINE_GAME_MODULE_DIR")
    fi
    if ! cmake "${cmake_args[@]}"; then
      fail 10 "configure"
    fi
  fi
  echo "== build =="
  build_targets=("${test_targets[@]}")
  [[ "$mode" == "full" ]] && build_targets+=(OctarineEngine OctarineBenchmarks)
  target_args=()
  ninja_targets="$(ninja -C "$build_dir" -t targets 2>/dev/null || true)"
  for t in "${build_targets[@]}"; do
    if [[ -z "$ninja_targets" ]] || echo "$ninja_targets" | grep -q "^$t: "; then
      target_args+=(--target "$t")
    fi
  done
  if ! cmake --build "$build_dir" "${target_args[@]}" --parallel; then
    fail 11 "build"
  fi
fi

# --- ctest -------------------------------------------------------------------
echo "== ctest =="
ctest_args=(--test-dir "$build_dir" --output-on-failure)
if [[ -n "$test_regex" ]]; then
  ctest_args+=(-R "$test_regex")
fi
if ! ctest "${ctest_args[@]}"; then
  fail 12 "ctest"
fi

# --- full-only gates ---------------------------------------------------------
if [[ "$mode" == "full" ]]; then
  echo "== bake smoke (offline fixture) =="
  game_path=""
  if [[ -n "${OCT_VERIFY_GAME:-}" ]]; then
    if [[ ! -d "$OCT_VERIFY_GAME" ]]; then
      echo "  -> OCT_VERIFY_GAME does not exist: $OCT_VERIFY_GAME" >&2
      fail 2 "bake-setup"
    else
      game_path="$(cd "$OCT_VERIFY_GAME" && pwd)"
    fi
  else
    fixture_src="$repo_root/tests/fixtures/bake-project"
    if [[ ! -d "$fixture_src" ]]; then
      echo "  -> built-in bake fixture missing at $fixture_src" >&2
      fail 2 "bake-setup"
    else
      stage_dir="$build_dir/bake-smoke-fixture"
      rm -rf "$stage_dir"
      mkdir -p "$stage_dir"
      cp -R "$fixture_src"/. "$stage_dir"/
      game_path="$stage_dir"
    fi
  fi

  if [[ -n "$game_path" && -d "$game_path" ]]; then
    if [[ ! -x "$binary" ]]; then
      echo "  -> engine binary not found at $binary (build --full first)" >&2
      fail 13 "bake"
    elif SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$binary" "$game_path" -m bake \
         && [[ -s "$game_path/asset_manifest.lua" ]] \
         && [[ -s "$game_path/asset_bundle.pak" ]]; then
      echo "  ok   bake produced non-empty asset_manifest.lua and asset_bundle.pak"
    else
      fail 13 "bake"
    fi
  fi

  echo "== Lua API drift =="
  drift_paths=(generated/lua_api.smoke.lua generated/components.json generated/modules.json)
  if git diff --quiet -- "${drift_paths[@]}"; then
    echo "  ok   no Lua API artifact drift"
  else
    echo "  -> regenerated stubs differ - commit them:" >&2
    git diff --stat -- "${drift_paths[@]}" >&2
    fail 14 "lua-api-drift"
  fi

  echo "== clang-tidy (changed files) =="
  check_tidy
fi

# --- architecture & purity guardrails (both modes) --------------------------
echo "== architecture & layer purity =="
if python3 scripts/check_architecture.py; then
  echo "  ok   clean layer DAG, CMake parity, genre purity, doc parity"
else
  echo "  -> architecture check failed" >&2
  fail 17 "architecture-drift"
fi

echo "== system & event catalog drift =="
if python3 scripts/gen_api_catalogs.py --check; then
  echo "  ok   no system/event catalog drift"
else
  echo "  -> regenerated catalogs differ - run: python3 scripts/gen_api_catalogs.py" >&2
  fail 16 "system-event-catalog-drift"
fi

# --- clang-format (both modes) ----------------------------------------------
echo "== clang-format (changed files) =="
check_format

verdict
exit "$first_fail_code"
