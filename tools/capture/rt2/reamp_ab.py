"""The RT-2 reamps against NAM-X's model: the model rendered from each take's played signal at the take's
settings; stereo WAV pairs (pedal, model) for listening, and the comparisons rotor phase doesn't affect:
the third-octave tone (model − pedal, dB), the level, and the A/B correlation (the stereo width).
  reamp_ab.py [SESSION]   (default the first reamp; the clean one is captures/boss/rt-2/2026-11-07-reamp3)

Run from the repository root, with the engine built as build-rt2/bin/render_rt2 (the command in tools/capture/render_rt2.cpp's header, -o build-rt2/bin/render_rt2)."""
import json, pathlib, subprocess, sys
import numpy as np, scipy.signal, soundfile

SESSION = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "captures/boss/rt-2/2026-11-07-reamp")
OUT = SESSION / "ab"
REF_PLAYED = 10 ** (-4.67 / 20)       # the rig's reference (the guitar's peak) as played
REF_NAMX = 10 ** (-6.0 / 20)          # NAM-X's reference
CHAIN = 10 ** (-8.89 / 20)            # recorded / played through the bypassed pedal
SLOW = {"7:00": 0.0, "9:00": 0.2445, "12:00": 0.811, "3:00": 1.959, "5:00": 2.5}
FAST = {"7:00": 2.5, "9:00": 4.099, "12:00": 6.845, "3:00": 9.024, "5:00": 10.0}
DRIVE = {"7:00": 0.0, "9:00": 0.2, "12:00": 0.5, "3:00": 0.8, "5:00": 1.0}
MODE = {"I": 0, "II": 1, "III": 2}
BANDS = [80, 125, 200, 315, 500, 800, 1250, 2000, 3150, 5000, 8000, 12500]

def third_octave(x):
    f, p = scipy.signal.welch(x, 48000, nperseg=8192)
    return np.array([10 * np.log10(p[(f >= b / 2 ** (1 / 6)) & (f < b * 2 ** (1 / 6))].mean() + 1e-30) for b in BANDS])

OUT.mkdir(exist_ok=True)
rows = []
for meta_path in sorted(SESSION.glob("*.json")):
    if meta_path.name == "rig.json":
        continue
    meta = json.loads(meta_path.read_text())
    if meta.get("bypass") or "noise" in meta_path.stem:
        continue
    s = meta["settings"]
    fast = s["LED"] == "red"
    horn = (FAST if fast else SLOW)[s["Fast" if fast else "Slow"]]
    drum = horn / (1.0871 if fast else 1.0270)
    ring, pos = s["Drive/Balance"].split()
    drive = DRIVE[pos] if ring == "DRIVE" else 0.0
    data, rate = soundfile.read(SESSION / f"{meta_path.stem}.wav", dtype="float64")
    played, _ = soundfile.read(SESSION / "played" / f"{meta_path.stem}.wav", dtype="float32")
    lat = meta["latency_samples"]
    x_namx = (played * (REF_NAMX / REF_PLAYED)).astype(np.float32)
    cmd = ["build-rt2/bin/render_rt2", str(MODE[s["Mode"]]), str(horn), str(drum), "0.0", "0.25",
           f"drive={drive}"] + (["fast=1"] if fast else [])
    y = np.frombuffer(subprocess.run(cmd, input=x_namx.tobytes(), capture_output=True, check=True).stdout,
                      dtype=np.float32).reshape(-1, 2).astype(np.float64)
    model = y * (REF_PLAYED / REF_NAMX) * CHAIN           # in the recording's units
    n = min(len(played), len(data) - lat)
    pedal = data[lat:lat + n][:, [0, 2]]
    model = model[:n]
    a, b = meta["signal_start"], n
    pedal, model = pedal[a:b], model[a:b]
    level = 10 * np.log10(np.mean(model ** 2) / np.mean(pedal ** 2))
    tone = [third_octave(model[:, c]) - third_octave(pedal[:, c]) for c in (0, 1)]
    corr_p = np.corrcoef(pedal[:, 0], pedal[:, 1])[0, 1]
    corr_m = np.corrcoef(model[:, 0], model[:, 1])[0, 1]
    rows.append((meta_path.stem, level, tone, corr_p, corr_m))
    gain = 0.5 / max(np.abs(pedal).max(), np.abs(model).max())
    soundfile.write(OUT / f"{meta_path.stem}.pedal.wav", pedal * gain, 48000, subtype="FLOAT")
    soundfile.write(OUT / f"{meta_path.stem}.model.wav", model * gain, 48000, subtype="FLOAT")
print(f"{'take':26s} level  A↔B corr pedal/model   tone, model − pedal (dB) at " + " ".join(f"{b:>5}" for b in BANDS))
for name, level, tone, cp, cm in rows:
    for c, label in ((0, "A"), (1, "B")):
        head = f"{name:26s} {level:+5.1f}  {cp:5.2f} / {cm:5.2f}" if c == 0 else " " * 52
        print(f"{head[:52]:52s} {label} " + " ".join(f"{t:+5.1f}" for t in tone[c]))
