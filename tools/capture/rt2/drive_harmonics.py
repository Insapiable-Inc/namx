"""The pedal's clipper alone, from the first session's 441 Hz steps (rotors at Slow 12:00, Balance 12:00): each
harmonic's level at the output, minus the rotors' power gain at its frequency (the same session's noise take, same
Mode): what the clipper put out, dB (one constant off per Mode). Saved for drive_fit.py
(build-rt2/fit/drive_harm.json). Run from the repository root."""
import json, pathlib, numpy as np, scipy.signal, soundfile
S = pathlib.Path("captures/boss/rt-2/2026-11-07")
K = 9
def load(name):
    m = json.loads((S / f"{name}.json").read_text())
    y, _ = soundfile.read(S / f"{name}.wav", dtype="float64"); x, _ = soundfile.read(S / "played" / f"{name}.wav", dtype="float64")
    return m, y[m["latency_samples"]:, 0], x
def gain_curve(name):
    m, y, x = load(name)
    a = m["signal_start"] + 4800; n = min(len(x), len(y)) - a - 4800
    f, py = scipy.signal.welch(y[a:a + n], 48000, nperseg=16384); _, px = scipy.signal.welch(x[a:a + n], 48000, nperseg=16384)
    return lambda fr: 10 * np.log10(py[(f > fr - 20) & (f < fr + 20)].mean() / px[(f > fr - 20) & (f < fr + 20)].mean())
out = {}
for mode, noise, takes in (("m1", "m1-slow-noise", ["m1-slow-steps", "m1-drive-0900-steps", "m1-drive-1200-steps", "m1-drive-0300-steps", "m1-drive-0500-steps"]),
                           ("m3", "m3-slow-noise", ["m3-drive-0700-steps", "m3-drive-0900-steps", "m3-drive-1200-steps", "m3-drive-0300-steps", "m3-drive-0500-steps"]),
                           ("m2", "m2-slow-noise", ["m2-slow-steps"])):
    g = gain_curve(noise)
    for name in takes:
        m, y, _ = load(name); spec = m["signal"]; f0 = spec["freq"]
        G = np.array([g(k * f0) for k in range(1, K + 1)])
        t = m["signal_start"]; rows = []
        for lv in spec["levels_db"]:
            a = t + int(0.3 * 48000); n = int((spec["step_seconds"] - 0.5) * 48000); w = np.hanning(n)
            F = np.abs(np.fft.rfft(y[a:a + n] * w)) ** 2 / np.sum(w ** 2) / n; f = np.fft.rfftfreq(n, 1 / 48000)
            h = np.array([10 * np.log10(F[(f > k * f0 - 8) & (f < k * f0 + 8)].sum() + 1e-30) for k in range(1, K + 1)])
            rows.append(h - G)
            t += int((spec["step_seconds"] + spec["gap_seconds"]) * 48000)
        out[name] = dict(levels=spec["levels_db"], harm=np.array(rows).tolist(), settings=m["settings"])
        print(name, m["settings"]["Drive/Balance"], m["settings"]["Mode"])
        for lv, r in zip(spec["levels_db"], rows):
            print(f"   {lv:+4d}  H1 {r[0]:6.1f}  re H1: " + " ".join(f"{v - r[0]:+5.0f}" for v in r[1:]))
pathlib.Path("build-rt2/fit").mkdir(parents=True, exist_ok=True)
pathlib.Path("build-rt2/fit/drive_harm.json").write_text(json.dumps(out))
