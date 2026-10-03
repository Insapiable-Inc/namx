#!/usr/bin/env python3
"""Per-effect CPU in every routing, for BENCHMARKS.md (Phase 27).

Each effect runs alone (NAMPlayerBench --only NAME), pinned to one CPU, in three
routings: mono in with its Stereo switch off, mono in with the switch on (only effects
that have one), and stereo in (every effect per channel; the amp both summed to mono
and per channel). Prints a Markdown table of mean µs per block and % of the budget.

    gui/bench/bench_effects.py --bench gui/build-bench/NAMPlayerBench --cpu 2 \
        --model amp.nam --ir cab.wav [--frames 128] [--rate 48000]
"""

import argparse
import os
import re
import subprocess

STAGES = ["Tuner", "Transpose", "Gate", "Compressor", "Wah", "Flanger", "Phaser", "Drive",
          "Amp", "Volume", "EQ", "Chorus", "Tremolo", "Delay", "Reverb", "Cab", "Limiter"]
HAS_STEREO_SWITCH = {"Flanger", "Phaser", "Chorus", "Tremolo", "Delay", "Reverb"}


def measure(a, stage, *flags):
    env = dict(os.environ, QT_LOGGING_RULES="*=false")
    cmd = ["taskset", "-c", str(a.cpu), a.bench, "--only", stage, "--frames", str(a.frames),
           "--rate", str(a.rate), "--model", a.model, "--ir", a.ir, *flags]
    out = subprocess.run(cmd, capture_output=True, text=True, env=env, check=True).stdout
    for line in out.splitlines():
        if line.split() and line.split()[0] == stage:
            mean, _p99, _max, pct = re.findall(r"[\d.]+", line)[:4]
            return f"{float(mean):.1f} µs · {float(pct):.2f} %"
    raise RuntimeError(f"no {stage} line in:\n{out}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bench", required=True)
    ap.add_argument("--cpu", type=int, default=2)
    ap.add_argument("--model", required=True)
    ap.add_argument("--ir", required=True)
    ap.add_argument("--frames", type=int, default=128)
    ap.add_argument("--rate", type=int, default=48000)
    a = ap.parse_args()

    print("| Effect | Mono | Stereo switch on | Stereo input |")
    print("|---|---|---|---|")
    for stage in STAGES:
        mono = measure(a, stage, "--stereo-modes", "off")
        switch = measure(a, stage, "--stereo-modes", "on") if stage in HAS_STEREO_SWITCH else "—"
        if stage == "Amp":
            summed = measure(a, stage, "--stereo-input")
            both = measure(a, stage, "--stereo-input", "--stereo-amp")
            stereo_in = f"{summed} (summed) · {both} (Stereo on)"
        else:
            stereo_in = measure(a, stage, "--stereo-input")
        print(f"| {stage} | {mono} | {switch} | {stereo_in} |", flush=True)


if __name__ == "__main__":
    main()
