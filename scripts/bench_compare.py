#!/usr/bin/env python3
"""Summarise or compare OctarineBenchmarks (Google Benchmark) JSON results as markdown.

Usage:
  scripts/bench_compare.py current.json                      # summary of one run
  scripts/bench_compare.py current.json --baseline base.json # diff against a baseline
  scripts/bench_compare.py current.json --baseline base.json --threshold 5 --fail-on-regression

Designed for headless/agent use: output is plain markdown on stdout, exit code 0 unless
--fail-on-regression is set and a benchmark regressed beyond the threshold.

When the JSON contains repetitions, the median aggregate is used and the coefficient of
variation (cv) is reported so noisy rows can be discounted.
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import OrderedDict

TIME_TO_NS = {"ns": 1.0, "us": 1e3, "ms": 1e6, "s": 1e9}

# Counters worth surfacing per tier, in display order. Lower is better for all except
# max_entities (higher is better) and emitted/entities/archetypes (informational).
KEY_COUNTERS = [
    "ns_per_entity", "budget60_pct", "p50_ms", "p95_ms", "p99_ms", "max_ms",
    "over60_pct", "over120_pct", "jitter_p95_ms",
    "allocs", "alloc_bytes", "lua_allocs", "lua_alloc_bytes", "lua_gc_kb",
    "chunks", "chunk_kb", "chunk_fill_pct", "active_fill_pct",
    "draw_batches", "keys_per_batch", "vertices", "tex_switches", "blend_switches",
    "ipc", "inst_per_entity", "cycles_per_entity", "bw_gbps",
    "max_entities", "entities", "archetypes", "emitted", "sort_us", "flush_us"
]
HIGHER_IS_BETTER = {
    "max_entities", "items_per_second", "chunk_fill_pct", "active_fill_pct",
    "keys_per_batch", "ipc", "bw_gbps"
}
INFORMATIONAL = {
    "entities", "archetypes", "emitted", "probes", "churn_per_frame",
    "chunk_kb", "vertices", "sort_us", "flush_us"
}


def load(path: str) -> tuple["OrderedDict[str, dict]", "OrderedDict[str, dict]"]:
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    rows: "OrderedDict[str, dict]" = OrderedDict()
    big_o_rows: "OrderedDict[str, dict]" = OrderedDict()
    benchmarks = data.get("benchmarks", [])
    aggregated = {b.get("run_name", b["name"]) for b in benchmarks if b.get("run_type") == "aggregate"}
    for b in benchmarks:
        if b.get("error_occurred"):
            continue
        name = b.get("run_name", b["name"])
        raw_name = b.get("name", "")
        run_type = b.get("run_type")
        agg = b.get("aggregate_name")

        if agg == "BigO" or raw_name.endswith("_BigO"):
            family = raw_name[:-5] if raw_name.endswith("_BigO") else name
            big_o_rows.setdefault(family, {})["big_o"] = b.get("big_o", "O(N)")
            if "cpu_coefficient" in b:
                big_o_rows[family]["cpu_coefficient"] = float(b["cpu_coefficient"])
            if "real_coefficient" in b:
                big_o_rows[family]["real_coefficient"] = float(b["real_coefficient"])
            continue

        if agg == "RMS" or raw_name.endswith("_RMS"):
            family = raw_name[:-4] if raw_name.endswith("_RMS") else name
            big_o_rows.setdefault(family, {})["rms"] = float(b.get("rms", 0.0))
            continue

        if name in aggregated:
            if run_type != "aggregate":
                continue
            if agg == "median":
                rows.setdefault(name, {}).update(_normalise(b))
            elif agg == "cv":
                rows.setdefault(name, {})["cv_pct"] = float(b.get("real_time", 0.0)) * 100.0
        else:
            rows[raw_name] = _normalise(b)
    return rows, big_o_rows


def _normalise(b: dict) -> dict:
    unit = TIME_TO_NS.get(b.get("time_unit", "ns"), 1.0)
    out = {"real_ns": float(b.get("real_time", 0.0)) * unit, "label": b.get("label", "")}
    skip = {"name", "run_name", "run_type", "repetitions", "repetition_index", "threads", "iterations",
            "real_time", "cpu_time", "time_unit", "family_index", "per_family_instance_index",
            "aggregate_name", "aggregate_unit", "label", "error_occurred", "error_message"}
    for k, v in b.items():
        if k not in skip and isinstance(v, (int, float)):
            out[k] = float(v)
    # Inverted-rate counters are stored in seconds; present them in nanoseconds.
    if "ns_per_entity" in out:
        out["ns_per_entity"] *= 1e9
    # Convert Google Benchmark bytes_per_second to gigabytes per second (GB/s).
    if "bytes_per_second" in out and "bw_gbps" not in out:
        out["bw_gbps"] = out["bytes_per_second"] / 1e9
    # Share of a 60 Hz frame one iteration consumes (only meaningful for per-frame tiers).
    if b["name"].split("/", 1)[0] in ("Sys", "Pipeline", "Frame"):
        out["budget60_pct"] = out["real_ns"] / (1e9 / 60.0) * 100.0
    return out


def fmt_time(ns: float) -> str:
    ans = abs(ns)
    if ans >= 1e9:
        return f"{ns / 1e9:.2f} s"
    if ans >= 1e6:
        return f"{ns / 1e6:.2f} ms"
    if ans >= 1e3:
        return f"{ns / 1e3:.1f} µs"
    if ans >= 100:
        return f"{ns:.0f} ns"
    if ans >= 10:
        return f"{ns:.1f} ns"
    return f"{ns:.2f} ns"


def fmt_num(v: float) -> str:
    if v == 0:
        return "0"
    if abs(v) >= 1e6:
        return f"{v / 1e6:.2f}M"
    if abs(v) >= 1e4:
        return f"{v / 1e3:.1f}k"
    if abs(v) >= 100:
        return f"{v:.0f}"
    if abs(v) >= 1:
        return f"{v:.2f}"
    return f"{v:.3f}"


def fmt_counter(key: str, v: float) -> str:
    if key.endswith("_pct"):
        return f"{v:.1f}%"
    if key.endswith("_bytes"):
        if abs(v) >= 1024 * 1024:
            return f"{v / (1024 * 1024):.2f} MB"
        if abs(v) >= 1024:
            return f"{v / 1024:.1f} KB"
        return f"{v:.0f} B"
    if key.endswith("_kb"):
        if abs(v) >= 1024:
            return f"{v / 1024:.2f} MB"
        return f"{v:.1f} KB"
    if key.endswith("_ms"):
        return f"{v:.2f} ms"
    if key.endswith("_us"):
        return f"{v:.1f} µs"
    if key in ("allocs", "lua_allocs", "draw_batches", "chunks", "entities", "max_entities", "vertices",
               "tex_switches", "blend_switches"):
        return f"{int(round(v))}"
    if key in ("ipc", "bw_gbps"):
        return f"{v:.2f}"
    return fmt_num(v)


def tier(name: str) -> str:
    return name.split("/", 1)[0]


def system_breakdown(row: dict, top: int = 6) -> str:
    sys_rows = sorted(((k[4:-3], v) for k, v in row.items() if k.startswith("sys_") and k.endswith("_us")),
                      key=lambda kv: kv[1], reverse=True)
    if not sys_rows:
        return ""
    return ", ".join(f"{name} {v:.0f}µs" for name, v in sys_rows[:top] if v >= 0.5)


def summary(rows: "OrderedDict[str, dict]", big_o_rows: "OrderedDict[str, dict] | None" = None) -> str:
    lines = []
    by_tier: "OrderedDict[str, list]" = OrderedDict()
    for name, row in rows.items():
        by_tier.setdefault(tier(name), []).append((name, row))
    for t, items in by_tier.items():
        lines.append(f"\n### {t}\n")
        counters = [c for c in KEY_COUNTERS if any(c in r for _, r in items)]
        has_sys = any(any(k.startswith("sys_") for k in r) for _, r in items)
        header = ["Benchmark", "Time"] + counters + (["Top systems"] if has_sys else [])
        lines.append("| " + " | ".join(header) + " |")
        lines.append("|" + "---|" * len(header))
        for name, row in items:
            label = f" ({row['label']})" if row.get("label") else ""
            cells = [f"`{name}`{label}", fmt_time(row["real_ns"])]
            cells += [fmt_counter(c, row[c]) if c in row else "" for c in counters]
            if has_sys:
                cells.append(system_breakdown(row))
            lines.append("| " + " | ".join(cells) + " |")

    if big_o_rows:
        lines.append("\n### Asymptotic Complexity (Big-O)\n")
        lines.append("| Family | Complexity | RMS Fit Error | Multiplier |")
        lines.append("|---|---|---|---|")
        for fam, bo in big_o_rows.items():
            comp = bo.get("big_o", "O(N)")
            rms = f"{bo.get('rms', 0.0) * 100.0:.1f}%" if "rms" in bo else "—"
            coeff = bo.get("real_coefficient") if bo.get("real_coefficient") is not None else bo.get("cpu_coefficient")
            coeff_str = fmt_time(coeff) if coeff is not None else "—"
            lines.append(f"| `{fam}` | `{comp}` | {rms} | {coeff_str} |")

    return "\n".join(lines)


COMPARE_METRICS = [
    ("time", "real_ns"),
    ("allocs", "allocs"),
    ("alloc_bytes", "alloc_bytes"),
    ("p95_ms", "p95_ms"),
    ("jitter_p95_ms", "jitter_p95_ms"),
    ("chunk_fill_pct", "chunk_fill_pct"),
    ("draw_batches", "draw_batches"),
    ("max_entities", "max_entities"),
]


def compare(current: "OrderedDict[str, dict]", baseline: "OrderedDict[str, dict]", threshold: float):
    lines = ["| Benchmark | Metric | Baseline | Current | Δ | |", "|---|---|---|---|---|---|"]
    regressions = []
    improvements = 0
    for name, cur in current.items():
        base = baseline.get(name)
        if base is None:
            lines.append(f"| `{name}` | time | — | {fmt_time(cur['real_ns'])} | new | |")
            continue
        for label, key in COMPARE_METRICS:
            if key not in cur or key not in base:
                continue
            b, c = base[key], cur[key]
            if key == "allocs":
                if b == 0 and c == 0:
                    continue
                if b == 0 and c > 0:
                    regressions.append((name, label, 100.0))
                    lines.append(f"| `{name}` | {label} | 0 | {int(round(c))} | +new allocs | 🔴 |")
                    continue
                if b > 0 and c == 0:
                    improvements += 1
                    lines.append(f"| `{name}` | {label} | {int(round(b))} | 0 | -100.0% | 🟢 |")
                    continue
            if b == 0:
                continue
            delta = (c - b) / b * 100.0
            better = delta > 0 if key in HIGHER_IS_BETTER else delta < 0
            noisy = max(cur.get("cv_pct", 0.0), base.get("cv_pct", 0.0)) > threshold
            flag = ""
            if abs(delta) >= threshold and not noisy:
                flag = "🟢" if better else "🔴"
                if better:
                    improvements += 1
                else:
                    regressions.append((name, label, delta))
            elif abs(delta) >= threshold and noisy:
                flag = "〰️ noisy"
            fmt_fn = fmt_time if key == "real_ns" else (lambda v: fmt_counter(key, v))
            lines.append(f"| `{name}` | {label} | {fmt_fn(b)} | {fmt_fn(c)} | {delta:+.1f}% | {flag} |")
    missing = [n for n in baseline if n not in current]
    head = [f"**{len(regressions)} regression(s), {improvements} improvement(s)** "
            f"beyond ±{threshold:.0f}% (rows with cv above the threshold are marked noisy, not flagged)."]
    if regressions:
        worst = sorted(regressions, key=lambda r: -abs(r[2]))[:10]
        head.append("\nWorst regressions:\n" + "\n".join(f"- `{n}` {m} {d:+.1f}%" for n, m, d in worst))
    if missing:
        head.append(f"\n{len(missing)} baseline benchmark(s) not present in the current run.")
    return "\n".join(head) + "\n\n" + "\n".join(lines), regressions


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("current", help="Google Benchmark JSON (--benchmark_out_format=json)")
    parser.add_argument("--baseline", help="baseline JSON to compare against")
    parser.add_argument("--threshold", type=float, default=5.0, help="percent change to flag (default 5)")
    parser.add_argument("--fail-on-regression", action="store_true", help="exit 1 if any flagged regression")
    args = parser.parse_args()

    current, current_big_o = load(args.current)
    if not current and not current_big_o:
        print(f"error: no benchmark rows in {args.current}", file=sys.stderr)
        return 2
    if args.baseline:
        base, _ = load(args.baseline)
        text, regressions = compare(current, base, args.threshold)
        print(text)
        return 1 if (args.fail_on_regression and regressions) else 0
    print(summary(current, current_big_o))
    return 0


if __name__ == "__main__":
    sys.exit(main())
