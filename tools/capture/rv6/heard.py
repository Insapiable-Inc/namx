#!/usr/bin/env python3
"""The RV-6's focused session as it is heard on guitar: the wet's energy per octave band over a take's
stabs and their tail, NAM-X less the pedal, dB — every 100 ms step counted, the pedal's floor under
NAM-X, and the same over the loudest tenth of the steps.

A band's mean over the steps where the pedal sounds (rv6/focused.py's guitar numbers, and the first
Shimmer fit's targets) can't see NAM-X ringing on after the pedal has gone quiet, and a mean of
decibels hides a wet that is louder at its top and quieter in its tail: this sum can.

  rv6/heard.py [--was BINARY] [TAKE ...]     (default: the Shimmer takes of rv6/ab.py)

Run from the repository root, with build-rv6/render_rv6 built."""
import pathlib, sys
import numpy as np
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import focused, takes

RATE = takes.RATE
STEP = RATE // 10
BANDS = [125, 250, 500, 1000, 2000, 4000, 8000]
TAKES = ["noon-guitar-shimmer", "time-0700-shimmer", "time-0300-shimmer", "tone-0700-shimmer", "tone-0500-shimmer",
         "check-a-shimmer"]


def levels(pair, at, length, centre):
    """The two outputs' mean power in 100 ms steps from `at`, one octave band."""
    power = 0.0
    for x in pair:
        y = focused.octave(x[at - RATE // 4:at + length], centre)[RATE // 4:]
        n = len(y) // STEP
        power = power + np.mean(y[:n * STEP].reshape(n, STEP) ** 2, axis=1)
    return 0.5 * power


def main():
    arguments = sys.argv[1:]
    builds = {"now": takes.RENDER}
    if "--was" in arguments:
        at = arguments.index("--was")
        builds = {"was": arguments[at + 1], "now": takes.RENDER}
        del arguments[at:at + 2]
    print(f"{'take':24s} {'':4s}" + " ".join(f"{b:>6d}" for b in BANDS) + "    all | the loudest tenth of the steps")
    for name in arguments or TAKES:
        meta, _, _, _, pre = takes.load(name)
        stabs = [(start, body, tail) for part, start, body, tail in focused.parts_of(meta)
                 if part["type"] == "file" and "stabs" in part["path"]]
        if not stabs:
            continue
        start, body, tail = stabs[0]
        length = body + min(tail, 6 * RATE)
        pedal = np.array([levels(takes.wet(name), start, length, c) for c in BANDS])
        floors = np.array([np.mean(focused.octave(pre - pre.mean(), c)[RATE // 10:] ** 2) for c in BANDS])
        for label, binary in builds.items():
            takes.RENDER = binary
            model = np.array([levels(takes.model_wet(name), start, length, c) for c in BANDS]) + floors[:, None]
            top = np.argsort(pedal.sum(axis=0))[-max(1, pedal.shape[1] // 10):]
            print(f"{name:24s} {label:4s}" + " ".join(f"{v:+6.1f}" for v in 10 * np.log10(model.sum(axis=1) / pedal.sum(axis=1)))
                  + f"  {10 * np.log10(model.sum() / pedal.sum()):+5.1f} | {10 * np.log10(model[:, top].sum() / pedal[:, top].sum()):+5.1f}")
        takes.RENDER = builds["now"]


if __name__ == "__main__":
    main()
