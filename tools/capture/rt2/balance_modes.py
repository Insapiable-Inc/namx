"""Per Mode, the follow-up's isolated rotors against its both-rotor take (all white noise, same session): the
pedal's power gain per third octave (recorded / played), horn alone (Balance 5:00) − both and drum alone (7:00) −
both, dB; and session 1's both-rotor noise (Balance 12:00) − the follow-up's, to see the two sessions agree."""
import json, pathlib
import numpy as np, scipy.signal, soundfile

R = pathlib.Path("captures/boss/rt-2")
BANDS = [100, 160, 250, 400, 630, 1000, 1600, 2500, 4000, 6300, 10000]

def gain(session, name, c):
    meta = json.loads((R / session / f"{name}.json").read_text())
    data, _ = soundfile.read(R / session / f"{name}.wav", dtype="float64")
    played, _ = soundfile.read(R / session / "played" / f"{name}.wav", dtype="float64")
    lat = meta["latency_samples"]
    a = meta["signal_start"] + 9600
    n = min(len(played), len(data) - lat)
    y, x = data[lat + a:lat + n, c], played[a:n]
    f, py = scipy.signal.welch(y, 48000, nperseg=8192)
    _, px = scipy.signal.welch(x, 48000, nperseg=8192)
    return np.array([10 * np.log10(py[(f >= b / 2 ** (1 / 6)) & (f < b * 2 ** (1 / 6))].mean()
                                   / px[(f >= b / 2 ** (1 / 6)) & (f < b * 2 ** (1 / 6))].mean()) for b in BANDS])

print(f"{'(dB)':34s}" + " ".join(f"{b:>6}" for b in BANDS))
for m in ("m1", "m2", "m3"):
    for c, out in ((0, "A"), (2, "B")):
        both = gain("2026-11-07-followup", f"{m}-both-noise", c)
        print(f"{m} {out}: horn − both (follow-up)        " + " ".join(f"{v:+6.1f}" for v in gain("2026-11-07-followup", f"{m}-horn-noise", c) - both))
        print(f"{m} {out}: drum − both (follow-up)        " + " ".join(f"{v:+6.1f}" for v in gain("2026-11-07-followup", f"{m}-drum-noise", c) - both))
        print(f"{m} {out}: session 1 both − follow-up both" + " ".join(f"{v:+6.1f}" for v in gain("2026-11-07", f"{m}-slow-noise", c) - both))
