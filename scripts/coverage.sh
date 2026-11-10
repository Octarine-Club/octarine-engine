#!/usr/bin/env bash
# Source-based code coverage runner and diff-coverage gate for Octarine Engine.
#
# Usage:
#   scripts/coverage.sh [options]
#
# Options:
#   --diff [base]     Run diff-coverage audit against base ref (default: HEAD~1 or PR merge base)
#   --threshold <N>   Diff coverage minimum threshold percentage (default: 80)
#   --summary         Print terminal summary table for all files in src/
#   --html [dir]      Generate HTML coverage report into directory (default: build/coverage_html)
#   --no-run          Skip ctest execution and reuse existing profile data
#   -h, --help        Show this help message

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
cd "$repo_root"

preset="player-debug"
build_dir="$repo_root/build/$preset"
raw_dir="$build_dir/profraw"
profdata="$build_dir/coverage.profdata"
coverage_lcov="$build_dir/coverage.lcov"

mode_diff=0
base_ref=""
threshold="80.0"
filter_dir="src/"
exclude_pattern=""
mode_summary=0
mode_html=0
html_dir="$build_dir/coverage_html"
run_tests=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --diff)
      mode_diff=1
      if [[ $# -gt 1 && ! "$2" =~ ^-- ]]; then
        base_ref="$2"
        shift
      fi
      ;;
    --threshold)
      threshold="$2"
      shift
      ;;
    --filter-dir)
      filter_dir="$2"
      shift
      ;;
    --exclude-pattern)
      exclude_pattern="$2"
      shift
      ;;
    --summary)
      mode_summary=1
      ;;
    --html)
      mode_html=1
      if [[ $# -gt 1 && ! "$2" =~ ^-- ]]; then
        html_dir="$2"
        shift
      fi
      ;;
    --no-run)
      run_tests=0
      ;;
    -h|--help)
      sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "error: unknown argument '$1' (try --help)" >&2
      exit 2
      ;;
  esac
  shift
done

# Find llvm-profdata and llvm-cov
PROFDATA=""
COV=""
if command -v xcrun &>/dev/null && xcrun --find llvm-profdata &>/dev/null; then
  PROFDATA="xcrun llvm-profdata"
  COV="xcrun llvm-cov"
elif command -v llvm-profdata &>/dev/null && command -v llvm-cov &>/dev/null; then
  PROFDATA="llvm-profdata"
  COV="llvm-cov"
else
  for ver in 19 18 17 16 15; do
    if command -v "llvm-profdata-$ver" &>/dev/null && command -v "llvm-cov-$ver" &>/dev/null; then
      PROFDATA="llvm-profdata-$ver"
      COV="llvm-cov-$ver"
      break
    fi
  done
fi

if [[ -z "$PROFDATA" || -z "$COV" ]]; then
  echo "error: llvm-profdata and llvm-cov tools not found on PATH." >&2
  exit 1
fi

if [[ $run_tests -eq 1 ]]; then
  rm -rf "$raw_dir"
  mkdir -p "$raw_dir"
  export LLVM_PROFILE_FILE="$raw_dir/%p-%m.profraw"

  echo "== Running test suite under coverage instrumentation =="
  ctest --test-dir "$build_dir" --output-on-failure

  echo "== Merging raw profiles into $profdata =="
  # shellcheck disable=SC2086
  $PROFDATA merge -sparse "$raw_dir"/*.profraw -o "$profdata"
fi

if [[ ! -f "$profdata" ]]; then
  echo "error: profile data $profdata not found." >&2
  exit 1
fi

# Locate test executables in build dir to pass as coverage objects
bin_dir="$build_dir/bin/debug"
[[ ! -d "$bin_dir" ]] && bin_dir="$build_dir/bin"
test_bins=()
while IFS= read -r bin; do
  [[ -x "$bin" && ! "$bin" =~ \.dSYM$ ]] && test_bins+=("$bin")
done < <(find "$bin_dir" -maxdepth 2 -type f -name "Octarine*" | sort)

if [[ ${#test_bins[@]} -eq 0 ]]; then
  echo "error: no Octarine test binaries found in $bin_dir" >&2
  exit 1
fi

primary_bin="${test_bins[0]}"
obj_args=()
for ((i=1; i<${#test_bins[@]}; ++i)); do
  obj_args+=("-object=${test_bins[i]}")
done

# Export coverage data to LCOV tracefile
# shellcheck disable=SC2086
$COV export -format=lcov -instr-profile="$profdata" "$primary_bin" "${obj_args[@]}" -sources "$repo_root/src" > "$coverage_lcov"

if [[ $mode_summary -eq 1 || ($mode_diff -eq 0 && $mode_html -eq 0) ]]; then
  echo "== Engine Source Coverage Summary (src/) =="
  # shellcheck disable=SC2086
  $COV report -instr-profile="$profdata" "$primary_bin" "${obj_args[@]}" -sources "$repo_root/src"
fi

if [[ $mode_html -eq 1 ]]; then
  echo "== Generating HTML coverage report into $html_dir =="
  mkdir -p "$html_dir"
  # shellcheck disable=SC2086
  $COV show -format=html -output-dir="$html_dir" -instr-profile="$profdata" "$primary_bin" "${obj_args[@]}" -sources "$repo_root/src"
  echo "Report written to file://$html_dir/index.html"
fi

if [[ $mode_diff -eq 1 ]]; then
  if [[ -z "$base_ref" ]]; then
    base_ref="$(git merge-base HEAD origin/main 2>/dev/null || echo HEAD~1)"
  fi
  echo "== Evaluating Diff Coverage against $base_ref (Threshold: ${threshold}%) =="
  diff_cmd=(python3 "$repo_root/scripts/diff_coverage.py"
    --lcov "$coverage_lcov"
    --base "$base_ref"
    --threshold "$threshold"
    --filter-dir "$filter_dir"
  )
  [[ -n "$exclude_pattern" ]] && diff_cmd+=(--exclude-pattern "$exclude_pattern")
  "${diff_cmd[@]}"
fi
