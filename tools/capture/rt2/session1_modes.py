"""Session 1's Balance 12:00 / Drive 7:00 takes in every Mode against the engine, absolute (the chain from session 1's
bypass noise): model − pedal per third octave and output, the level, and the A↔B correlation (pedal / model).
  session1_modes.py [TAKE …]

Run from the repository root, with the engine built as build-rt2/bin/render_rt2 (the command in tools/capture/render_rt2.cpp's header, -o build-rt2/bin/render_rt2)."""
import json, pathlib, subprocess, sys
import numpy as np, scipy.signal, soundfile

S1 = pathlib.Path("captures/boss/rt-2/2026-11-07")
BANDS = [100, 160, 250, 400, 630, 1000, 1600, 2500, 3150, 4000, 5000, 6300, 8000, 10000]
REF_NAMX = 10 ** (-6.0 / 20)
ref = 10 ** (json.loads((S1 / "rig.json").read_text())["ref_dbfs"] / 20)
MODE = {"I": 0, "II": 1, "III": 2}

def take(name):
    meta = json.loads((S1 / f"{name}.json").read_text())
    data, _ = soundfile.read(S1 / f"{name}.wav", dtype="float64")
    played, _ = soundfile.read(S1 / "played" / f"{name}.wav", dtype="float64")
    lat = meta["latency_samples"]
    n = min(len(played), len(data) - lat)
    return meta, data[lat:lat + n], played[:n]

def bands(y):
    f, p = scipy.signal.welch(y, 48000, nperseg=8192)
    return np.array([10 * np.log10(p[(f >= b / 2 ** (1 / 6)) & (f < b * 2 ** (1 / 6))].mean() + 1e-30) for b in BANDS])

meta, rec, played = take("bypass-noise")
a = meta["signal_start"] + 4800
chain = np.sqrt(np.mean(rec[a:a + 8 * 48000, 0] ** 2) / np.mean(played[a:a + 8 * 48000] ** 2))
names = sys.argv[1:] or ["m1-slow-noise", "m1-fast-noise", "m2-slow-noise", "m3-slow-noise",
                         "m1-slow-reamp", "m1-fast-reamp", "m2-slow-reamp", "m2-fast-reamp", "m3-slow-reamp", "m3-fast-reamp"]
print(f"{'take':16s}     level  corr p/m   " + " ".join(f"{b:>6}" for b in BANDS))
for name in names:
    meta, rec, played = take(name)
    s = meta["settings"]
    fast = s.get("LED") == "red" or "-fast" in name
    horn_hz = 6.845 if fast else 0.811
    a = meta["signal_start"] + 9600
    b = len(played)
    cmd = ["build-rt2/bin/render_rt2", str(MODE[s["Mode"]]), str(horn_hz), str(horn_hz / (1.0871 if fast else 1.027)),
           "0.0", "0.25", "drive=0"] + (["fast=1"] if fast else [])
    y = np.frombuffer(subprocess.run(cmd, input=(played * (REF_NAMX / ref)).astype(np.float32).tobytes(),
                                     capture_output=True, check=True).stdout,
                      dtype=np.float32).reshape(-1, 2).astype(np.float64) * (ref / REF_NAMX) * chain
    p, m = rec[a:b][:, [0, 2]], y[a:b]
    level = 10 * np.log10(np.mean(m ** 2) / np.mean(p ** 2))
    cp, cm = np.corrcoef(p[:, 0], p[:, 1])[0, 1], np.corrcoef(m[:, 0], m[:, 1])[0, 1]
    for o in (0, 1):
        head = f"{name:16s} {'AB'[o]}  {level:+5.1f}  {cp:4.2f}/{cm:4.2f}" if o == 0 else f"{'':16s} B" + " " * 18
        print(f"{head:38s}" + " ".join(f"{v:+6.1f}" for v in bands(m[:, o]) - bands(p[:, o])))
