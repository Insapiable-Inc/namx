#!/usr/bin/env python3
"""Amp CPU matrix for BENCHMARKS.md (Phase 27).

Runs namx-bench's Amp stage for each model over buffer sizes and sample rates,
pinned to one CPU, and prints a Markdown table of mean µs per block and % of the
real-time budget. Deterministic input (the bench's own signal); no files written.

    gui/bench/bench_matrix.py --bench gui/build-bench/namx-bench --cpu 2 \
        --model "Standard WaveNet=path/a.nam" --model "Lite=path/b.nam"
"""

import argparse
import os
import re
import subprocess
import sys

FRAMES = [32, 64, 128, 256]
RATES = [44100, 48000, 96000]


def amp_line(bench, cpu, model, frames, rate):
    env = dict(os.environ, QT_LOGGING_RULES="*=false")
    cmd = ["taskset", "-c", str(cpu), bench, "--model", model, "--only", "Amp",
           "--frames", str(frames), "--rate", str(rate)]
    out = subprocess.run(cmd, capture_output=True, text=True, env=env, check=True).stdout
    for line in out.splitlines():
        if line.startswith("Amp"):
            # stage  mean µs  p99 µs  max µs  % budget
            mean, p99, _max, pct = re.findall(r"[\d.]+", line)[:4]
            return float(mean), float(p99), float(pct)
    raise RuntimeError("no Amp line in:\n" + out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bench", required=True)
    ap.add_argument("--cpu", type=int, default=2)
    ap.add_argument("--model", action="append", required=True, help="Label=path")
    a = ap.parse_args()

    print(f"| Model | Rate | " + " | ".join(f"{f} fr" for f in FRAMES) + " |")
    print("|---|---|" + "---|" * len(FRAMES))
    for spec in a.model:
        label, path = spec.split("=", 1)
        for rate in RATES:
            cells = []
            for frames in FRAMES:
                mean, _p99, pct = amp_line(a.bench, a.cpu, path, frames, rate)
                cells.append(f"{mean:.0f} µs · {pct:.1f} %")
            note = "" if rate == 48000 else " ¹"
            print(f"| {label} | {rate / 1000:g} kHz{note} | " + " | ".join(cells) + " |")
            sys.stdout.flush()


if __name__ == "__main__":
    main()
