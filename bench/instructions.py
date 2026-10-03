#!/usr/bin/env python3
"""CPU regression check by instruction count (Phase 27).

Runs every stage of namx-bench under valgrind's callgrind and counts the CPU
instructions one 128-frame block costs. Instruction counts are nearly deterministic for
a given binary, unlike wall time on shared CI machines. Startup (model load, IR build)
is cancelled out by running each stage for two block counts and taking the difference.

Counts depend on the compiler, so the baseline file keeps one entry per compiler (the
bench prints which one built it). Without an entry for this compiler the check only
reports; with one, a stage more than --tolerance above its baseline fails.

    gui/bench/instructions.py --bench gui/build-release/namx-bench \
        --baseline gui/bench/instructions-baseline.json [--update]

The amp runs example_models/wavenet_a1_standard.nam (a standard 16+8 ch WaveNet); the
cab gets a small IR this script writes into a temporary directory it creates.
"""

import argparse
import json
import math
import os
import re
import struct
import subprocess
import sys
import tempfile

# No Tuner: its pitch detection runs on a worker thread woken every 100 ms of wall time,
# so under valgrind its instruction count depends on how long the run takes. Its
# audio-thread part (handing over samples) is trivial.
STAGES = ["Transpose", "Gate", "Compressor", "Wah", "Flanger", "Phaser", "Drive", "Amp",
          "Volume", "EQ", "Chorus", "Tremolo", "Delay", "Reverb", "Cab", "Limiter"]
FEW, MANY = 50, 150  # block counts; the difference is MANY - FEW blocks


def write_ir(path):
    # A decaying cosine, 2048 taps, mono 32-bit float — deterministic.
    sr, n = 48000, 2048
    data = b"".join(struct.pack("<f", math.cos(2 * math.pi * 120 * i / sr) * math.exp(-i / 300))
                    for i in range(n))
    header = (b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " +
              struct.pack("<IHHIIHH", 16, 3, 1, sr, sr * 4, 4, 32) + b"data" +
              struct.pack("<I", len(data)))
    with open(path, "wb") as f:
        f.write(header + data)


def instructions(bench, args):
    cmd = ["valgrind", "--tool=callgrind", "--callgrind-out-file=/dev/null", bench, *args]
    env = dict(os.environ, QT_LOGGING_RULES="*=false")
    res = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if res.returncode != 0:
        raise RuntimeError(f"{' '.join(cmd)} failed:\n{res.stderr[-2000:]}")
    m = re.search(r"Collected\s*:\s*(\d+)", res.stderr)
    if not m:
        raise RuntimeError("no callgrind total in:\n" + res.stderr[-2000:])
    return int(m.group(1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bench", required=True)
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--update", action="store_true", help="write this compiler's baseline")
    ap.add_argument("--tolerance", type=float, default=0.03, help="allowed growth (0.03 = 3 %%)")
    a = ap.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    model = os.path.join(repo, "example_models", "wavenet_a1_standard.nam")
    head = subprocess.run([a.bench, "--only", "Gate", "--blocks", "1"], capture_output=True,
                          text=True, env=dict(os.environ, QT_LOGGING_RULES="*=false")).stdout
    compiler = re.search(r"^compiler: (.+)$", head, re.M).group(1).strip()

    with tempfile.TemporaryDirectory() as tmp:
        ir = os.path.join(tmp, "ir.wav")
        write_ir(ir)
        per_block = {}
        for stage in STAGES + ["TOTAL"]:
            # Loading a model is slow under valgrind: only the stages that use one get it.
            args = ["--stereo-modes", "on"]
            if stage in ("Amp", "TOTAL"):
                args += ["--model", model]
            if stage in ("Cab", "TOTAL"):
                args += ["--ir", ir]
            args += ["--only", stage if stage != "TOTAL" else ",".join(STAGES)]
            few = instructions(a.bench, args + ["--blocks", str(FEW)])
            many = instructions(a.bench, args + ["--blocks", str(MANY)])
            per_block[stage] = round((many - few) / (MANY - FEW))
            print(f"{stage:11} {per_block[stage]:>12,} instructions/block", flush=True)

    baselines = {}
    if os.path.exists(a.baseline):
        with open(a.baseline) as f:
            baselines = json.load(f)
    if a.update:
        baselines[compiler] = per_block
        with open(a.baseline, "w") as f:
            json.dump(baselines, f, indent=2, sort_keys=True)
            f.write("\n")
        print(f"\nbaseline written for: {compiler}")
        return 0

    base = baselines.get(compiler)
    if base is None:
        print(f"\nno baseline for '{compiler}' — reporting only. To record it, add this "
              f"entry to {os.path.basename(a.baseline)} (or run with --update on this "
              f"toolchain):")
        print(json.dumps({compiler: per_block}, indent=2, sort_keys=True))
        return 0
    failed = False
    print(f"\nagainst the baseline for {compiler} (tolerance {a.tolerance:.0%}):")
    for stage, now in per_block.items():
        before = base.get(stage)
        if not before:
            continue
        change = now / before - 1.0
        mark = "FAIL" if change > a.tolerance else "ok"
        failed |= change > a.tolerance
        print(f"  {mark:4} {stage:11} {change:+.1%}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
