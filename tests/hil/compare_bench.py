#!/usr/bin/env python3
# Copyright 2026 Isomet (UK) Ltd. All rights reserved.
"""Compare two Google Benchmark JSON files and flag regressions.

    compare_bench.py baseline.json current.json [--threshold 0.15] [--summary out.md]

For every benchmark the median real time across repetitions is compared. A benchmark
regresses when current > baseline * (1 + threshold). Benchmarks that report a non-zero
`failures` counter, or errored, also fail the comparison. Exit status is 1 on any failure
(unless --report-only), so the script can gate a CI job.
"""

import argparse
import json
import statistics
import sys

UNIT_NS = {"ns": 1.0, "us": 1e3, "ms": 1e6, "s": 1e9}


def load(path):
    """Returns ({run_name: {"time_ns": median, "failures": n, "error": msg}}, context)."""
    with open(path, encoding="utf-8") as f:
        data = json.load(f)

    runs = {}
    for b in data.get("benchmarks", []):
        if b.get("run_type") != "iteration" or b.get("skipped"):
            continue
        r = runs.setdefault(b["run_name"], {"times": [], "failures": 0.0, "error": None})
        if b.get("error_occurred"):
            r["error"] = b.get("error_message", "error")
            continue
        r["times"].append(b["real_time"] * UNIT_NS[b.get("time_unit", "ns")])
        r["failures"] += b.get("failures", 0.0)

    out = {}
    for name, r in runs.items():
        out[name] = {
            "time_ns": statistics.median(r["times"]) if r["times"] else None,
            "failures": r["failures"],
            "error": r["error"],
        }
    return out, data.get("context", {})


def fmt_time(ns):
    if ns is None:
        return "-"
    for unit, scale in (("s", 1e9), ("ms", 1e6), ("us", 1e3)):
        if ns >= scale:
            return f"{ns / scale:.3g} {unit}"
    return f"{ns:.3g} ns"


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("baseline")
    p.add_argument("current")
    p.add_argument("--threshold", type=float, default=0.15, help="allowed relative slowdown (default 0.15)")
    p.add_argument("--summary", help="also write the Markdown table to this file (e.g. $GITHUB_STEP_SUMMARY)")
    p.add_argument("--report-only", action="store_true", help="never exit non-zero")
    args = p.parse_args()

    base, base_ctx = load(args.baseline)
    cur, cur_ctx = load(args.current)

    lines = [
        "### Benchmark comparison",
        "",
        f"Baseline: `{base_ctx.get('hil_target', '?')}` ({base_ctx.get('date', '?')})  ",
        f"Current: `{cur_ctx.get('hil_target', '?')}` ({cur_ctx.get('date', '?')})  ",
        f"Threshold: +{args.threshold:.0%}",
        "",
        "| Benchmark | Baseline | Current | Change | Status |",
        "|---|---:|---:|---:|---|",
    ]
    if base_ctx.get("hil_target") != cur_ctx.get("hil_target"):
        lines.insert(5, "> **Warning:** baseline and current were measured on different targets.\n")

    failed = 0
    for name in sorted(set(base) | set(cur)):
        b, c = base.get(name), cur.get(name)
        if c is None:
            lines.append(f"| {name} | {fmt_time(b['time_ns'])} | - | | missing |")
            continue
        if c["error"]:
            lines.append(f"| {name} | {fmt_time(b and b['time_ns'])} | - | | **error:** {c['error']} |")
            failed += 1
            continue
        if c["failures"]:
            lines.append(f"| {name} | {fmt_time(b and b['time_ns'])} | {fmt_time(c['time_ns'])} | | **{c['failures']:.0f} failed requests** |")
            failed += 1
            continue
        if b is None or not b["time_ns"]:
            lines.append(f"| {name} | - | {fmt_time(c['time_ns'])} | | new |")
            continue

        ratio = c["time_ns"] / b["time_ns"]
        change = f"{ratio - 1:+.1%}"
        if ratio > 1 + args.threshold:
            status = "**regression**"
            failed += 1
        elif ratio < 1 - args.threshold:
            status = "improved"
        else:
            status = "ok"
        lines.append(f"| {name} | {fmt_time(b['time_ns'])} | {fmt_time(c['time_ns'])} | {change} | {status} |")

    lines += ["", f"{failed} failing benchmark(s)." if failed else "No regressions."]
    report = "\n".join(lines) + "\n"
    print(report)
    if args.summary:
        with open(args.summary, "a", encoding="utf-8") as f:
            f.write(report)

    return 1 if failed and not args.report_only else 0


if __name__ == "__main__":
    sys.exit(main())
