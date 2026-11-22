#!/usr/bin/env python3
"""NAM-X's Phaser as it is against the pedal on the Res session's tone takes (2026-11-11-res): each tone's
level over the sweep cycle, dB rms (levels floored at -30 dB), the model's sweep slid in time to the best
fit. The model is the real one, rendered by build-ph3/render_ph3 from the take's played signal.

  ph3/baseline.py TAKE TYPE DEPTH RES [TAKE TYPE DEPTH RES ...]
  TYPE 1-7 = 4, 8, 10, 12 Stage, Rise, Fall, Step; DEPTH, RES 0-1 of the pedal's knob.
  e.g. ph3/baseline.py a-res700-8stage 2 1 0  a-res700-rise 5 1 0  a-res500-rise 5 1 1

Run from the repository root."""
import json, pathlib, subprocess, sys
import numpy as np, scipy.signal, soundfile
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import cycle

RATE = cycle.RATE


def model_gains(name, kind, depth, res):
    meta = json.loads((cycle.SESSION / f"{name}.json").read_text())
    played, _ = soundfile.read(cycle.SESSION / "played" / f"{name}.wav", dtype="float64")
    pedal, t = cycle.gains(name)
    p = cycle.period(pedal, t)
    scale = 10 ** ((-6.0 - meta["ref_dbfs"]) / 20)
    y = subprocess.run(["build-ph3/render_ph3", str(kind), f"{1 / p:.5f}", str(depth), str(res)],
                       input=(played * scale).astype(np.float32).tobytes(), capture_output=True, check=True).stdout
    y = np.frombuffer(y, np.float32).astype(float) / scale
    a = meta["signal_start"]
    n = int(meta["signal"]["seconds"] * RATE)
    tt = np.arange(n) / RATE
    out = []
    for f in cycle.TONES:
        gap = min(abs(f - q) for q in cycle.TONES if q != f)
        low = scipy.signal.butter(4, min(110, 0.42 * gap), fs=RATE, output="sos")
        e = np.exp(-2j * np.pi * f * tt)
        out.append((scipy.signal.sosfiltfilt(low, y[a:a + n] * e) /
                    scipy.signal.sosfiltfilt(low, played[a:a + n] * e))[4800:-4800:24])
    return np.array(out), pedal, t, p


def main():
    args = sys.argv[1:]
    for name, kind, depth, res in zip(args[0::4], args[1::4], args[2::4], args[3::4]):
        model, pedal, t, p = model_gains(name, int(kind), float(depth), float(res))
        lp = np.maximum(20 * np.log10(np.abs(pedal)), -30)
        lm = np.maximum(20 * np.log10(np.abs(model)), -30)
        n = int(p / (t[1] - t[0]))
        m = lp.shape[1] - n
        best = (1e9, None)
        for s in range(0, n, max(1, n // 300)):
            e = np.sqrt(np.mean((lm[:, s:s + m] - lp[:, :m]) ** 2, axis=1))
            if e[1:8].mean() < best[0]:
                best = (e[1:8].mean(), e)
        print(f"{name} against type {kind} (period {p:.3f} s): dB rms per tone "
              f"{' '.join(f'{v:4.1f}' for v in best[1])}   mean, 197 Hz-5.1 kHz: {best[0]:.1f}")


if __name__ == "__main__":
    main()
