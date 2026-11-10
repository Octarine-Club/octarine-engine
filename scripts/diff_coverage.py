#!/usr/bin/env python3
"""Diff-Coverage Auditor for Octarine Engine.

Computes code coverage on newly added or modified lines in src/ against a git
merge-base or reference commit using an LCOV tracefile from llvm-cov.
"""

import argparse
import os
import re
import subprocess
import sys


def parse_git_diff_hunks(base_ref, filter_dir="src/"):
    """Returns a dict mapping normalized relative filepath -> set of modified line numbers."""
    cmd = ["git", "diff", "-U0", base_ref, "--", filter_dir]
    try:
        res = subprocess.run(cmd, capture_output=True, text=True, check=True)
    except subprocess.CalledProcessError:
        cmd = ["git", "diff", "-U0", f"{base_ref}...HEAD", "--", filter_dir]
        res = subprocess.run(cmd, capture_output=True, text=True, check=True)

    diff_lines = res.stdout.splitlines()
    changed_lines_by_file = {}
    current_file = None

    hunk_regex = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")

    for line in diff_lines:
        if line.startswith("+++ b/"):
            current_file = os.path.normpath(line[6:])
            if current_file not in changed_lines_by_file:
                changed_lines_by_file[current_file] = set()
        elif line.startswith("@@ ") and current_file:
            m = hunk_regex.match(line)
            if m:
                start = int(m.group(1))
                count = int(m.group(2)) if m.group(2) is not None else 1
                for l_num in range(start, start + count):
                    changed_lines_by_file[current_file].add(l_num)

    return changed_lines_by_file


def parse_lcov_data(lcov_path):
    """Parses LCOV format file and returns dict: filepath -> {line_num: count}."""
    file_coverage = {}
    current_file = None
    with open(lcov_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("SF:"):
                current_file = os.path.normpath(line[3:])
                if current_file not in file_coverage:
                    file_coverage[current_file] = {}
            elif line.startswith("DA:") and current_file:
                parts = line[3:].split(",")
                if len(parts) >= 2:
                    try:
                        l_num = int(parts[0])
                        count = int(parts[1])
                        file_coverage[current_file][l_num] = count
                    except ValueError:
                        continue
            elif line == "end_of_record":
                current_file = None
    return file_coverage


def audit_diff_coverage(lcov_path, changed_lines_by_file, threshold=80.0, exclude_pattern=None):
    file_coverage = parse_lcov_data(lcov_path)

    total_diff_executable = 0
    total_diff_covered = 0
    file_results = []

    exclude_regex = re.compile(exclude_pattern) if exclude_pattern else None

    for rel_path, changed_lines in changed_lines_by_file.items():
        if not changed_lines:
            continue
        if exclude_regex and exclude_regex.search(rel_path):
            continue

        # Match rel_path against absolute or relative paths in file_coverage
        matched_lines = None
        for path_key, lines in file_coverage.items():
            if path_key.endswith(rel_path) or rel_path.endswith(path_key):
                matched_lines = lines
                break

        if not matched_lines:
            continue

        file_executable = 0
        file_covered = 0
        uncovered_lines = []

        for l_num in sorted(changed_lines):
            if l_num in matched_lines:
                file_executable += 1
                exec_count = matched_lines[l_num]
                if exec_count > 0:
                    file_covered += 1
                else:
                    uncovered_lines.append(l_num)

        if file_executable > 0:
            total_diff_executable += file_executable
            total_diff_covered += file_covered
            pct = (file_covered / file_executable) * 100.0
            file_results.append({
                "file": rel_path,
                "executable": file_executable,
                "covered": file_covered,
                "pct": pct,
                "uncovered": uncovered_lines,
            })

    print("================================================================================")
    print("                      OCTARINE ENGINE DIFF COVERAGE REPORT                      ")
    print("================================================================================")

    if total_diff_executable == 0:
        print("No modified executable lines found in diff. Diff coverage: 100.0% (PASS)")
        return 0

    file_results.sort(key=lambda r: (r["pct"], r["file"]))
    for r in file_results:
        unc_str = f" | Uncovered: {r['uncovered']}" if r["uncovered"] else ""
        print(f"  {r['file']:<50} {r['covered']:>3}/{r['executable']:<3} ({r['pct']:>5.1f}%){unc_str}")

    overall_pct = (total_diff_covered / total_diff_executable) * 100.0
    print("--------------------------------------------------------------------------------")
    print(f"TOTAL DIFF COVERAGE: {total_diff_covered}/{total_diff_executable} ({overall_pct:.2f}%) [Threshold: {threshold:.1f}%]")
    print("================================================================================")

    if overall_pct < threshold:
        print(f"FAILED: Diff coverage {overall_pct:.2f}% is below required threshold of {threshold:.1f}%", file=sys.stderr)
        return 1

    print("PASS: Diff coverage satisfies quality gate.")
    return 0


def main():
    parser = argparse.ArgumentParser(description="Audit diff code coverage against merge-base.")
    parser.add_argument("--lcov", required=True, help="Path to lcov info file")
    parser.add_argument("--base", default="HEAD~1", help="Base git commit / branch (default: HEAD~1)")
    parser.add_argument("--threshold", type=float, default=80.0, help="Required coverage %% (default: 80.0)")
    parser.add_argument("--filter-dir", default="src/", help="Directory to filter (default: src/)")
    parser.add_argument("--exclude-pattern", default=None, help="Regex pattern of paths to exclude from audit")

    args = parser.parse_args()

    changed_lines = parse_git_diff_hunks(args.base, args.filter_dir)
    sys.exit(audit_diff_coverage(args.lcov, changed_lines, args.threshold, args.exclude_pattern))


if __name__ == "__main__":
    main()
