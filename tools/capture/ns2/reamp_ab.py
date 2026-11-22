"""The NS-2 reamps against NAM-X's Noise Gate (the NS-2 model): NAM-X's own processor rendered from
each take's played signal at the take's settings (render_ns2), then compared with the pedal as a gain
over time — the output re what the open path would give (the bypass take's chain applied to the played
signal), in 20 ms windows, everything through one 60 Hz high-pass run forward only. Each time the pedal's
gate switches, its DC level steps: the interface's input coupling turns the step into a ~1 Hz swing (reported
apart as "wobble") and its edge is a click up to ~30 dB over the input; a two-way filter would carry both back
before the switch. The rig's floor (the pedal channel before the marker, through the same filter) is added to
the model's output, so both read the floor where the gate has pushed the signal under it. Left out: windows
whose open path isn't 12 dB over the floor, windows next to a level jump of 6 dB (a one-window slip there is
tens of dB), and windows where the pedal is louder than the open path (a click, not a gain). Writes WAV pairs
(pedal unfiltered, model) for listening in SESSION/ab/, and the gain traces per take (input level, pedal and
model gain, windows scored; 20 ms) in build-ns2/reamp-traces.npz.

  reamp_ab.py [SESSION]   (default captures/boss/ns-2/2026-11-08-reamp)

Run from the repository root, with build-ns2/render_ns2 built (the command in
tools/capture/render_ns2.cpp's header, -o build-ns2/render_ns2); NS2_RENDER names another build of it (an older
model's, to compare)."""
import json, os, pathlib, re, subprocess, sys
import numpy as np, scipy.signal, soundfile

SESSION = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "captures/boss/ns-2/2026-11-08-reamp")
OUT = SESSION / "ab"
RATE = 48000
REF_NAMX_DBFS = -6.0
CLOCKS = ["7:00", "9:00", "12:00", "3:00", "5:00"]
WINDOW = 960                      # 20 ms
OVER_FLOOR_DB = 12.0
JUMP_DB = 6.0
HIGH_PASS = scipy.signal.butter(4, 60, "highpass", fs=48000, output="sos")
WOBBLE = scipy.signal.butter(4, 10, "lowpass", fs=48000, output="sos")
BANDS = [80, 125, 200, 315, 500, 800, 1250, 2000, 3150, 5000, 8000, 12500]

# The model's knob tables, from the header NAM-X compiles.
HEADER = pathlib.Path("src/dsp/Ns2Gate.h").read_text()
def table(name):
    body = re.search(name + r"\{([^}]*)\}", HEADER).group(1)
    return [float(v.rstrip("f")) for v in body.split(",")]
THRESHOLD_DB = dict(zip(CLOCKS, table("THRESHOLD_DB")))
DECAY_MS = dict(zip(CLOCKS, table("DECAY_MS")))
OPEN_DB = float(re.search(r"OPEN_DB = ([-\d.]+)f", HEADER).group(1))


def db(power):
    return 10 * np.log10(power + 1e-30)


def window_power(x):
    n = len(x) // WINDOW
    return np.mean(x[:n * WINDOW].reshape(n, WINDOW) ** 2, axis=1)


def third_octave(x):
    f, p = scipy.signal.welch(x, RATE, nperseg=8192)
    return np.array([db(p[(f >= b / 2 ** (1 / 6)) & (f < b * 2 ** (1 / 6))].mean()) for b in BANDS])


def load(name):
    meta = json.loads((SESSION / f"{name}.json").read_text())
    recorded, _ = soundfile.read(SESSION / f"{name}.wav", dtype="float64")
    played, _ = soundfile.read(SESSION / "played" / f"{name}.wav", dtype="float64")
    lat = meta["latency_samples"]
    n = min(len(played), len(recorded) - lat)
    pedal = scipy.signal.sosfilt(HIGH_PASS, recorded[:, 0])
    # The floor: the stretch before the marker, through the same filter once it has settled (100 ms).
    return meta, pedal[lat:lat + n], played[:n], pedal[RATE // 10:meta["marker_start"]], recorded[lat:lat + n, 0]


def chain_filter():
    """The played → recorded response through the bypassed pedal (a 512-tap FIR, centred), from the
    unfiltered recording (the high-pass is applied once, to every side, afterwards)."""
    meta, _, played, _, raw = load("bypass-dynamics")
    pedal = raw - raw.mean()
    f, pxy = scipy.signal.csd(played, pedal, RATE, nperseg=8192)
    _, pxx = scipy.signal.welch(played, RATE, nperseg=8192)
    h = np.fft.irfft(pxy / np.maximum(pxx, 1e-30 * pxx.max()))
    h = np.roll(h, 256)[:512] * np.hanning(512)
    return h


def through(h, x):
    return scipy.signal.sosfilt(HIGH_PASS, np.convolve(x, h)[256:256 + len(x)])


def render(played, meta):
    s = meta["settings"]
    ref_played = 10 ** (meta["ref_dbfs"] / 20)
    ref_namx = 10 ** (REF_NAMX_DBFS / 20)
    opens_at = THRESHOLD_DB[s["Threshold"]] + OPEN_DB + REF_NAMX_DBFS
    x = (played * (ref_namx / ref_played)).astype(np.float32)
    y = subprocess.run([os.environ.get("NS2_RENDER", "build-ns2/render_ns2"), f"{opens_at}", f"{DECAY_MS[s['Decay']]}"], input=x.tobytes(),
                       capture_output=True, check=True).stdout
    return np.frombuffer(y, dtype=np.float32).astype(np.float64) * (ref_played / ref_namx)


def main():
    OUT.mkdir(exist_ok=True)
    h = chain_filter()
    rows, traces = [], {}
    for meta_path in sorted(SESSION.glob("threshold-*.json"), key=lambda p: json.loads(p.read_text())["recorded_at"]):
        name = meta_path.stem
        if name.endswith("-noise"):
            continue
        meta, pedal, played, pre, raw = load(name)
        s = meta["settings"]
        model = through(h, render(played, meta))
        open_path = through(h, played)
        a = meta["signal_start"]
        floor = np.mean(pre ** 2)
        p_pedal, p_model, p_open = (window_power(v[a:]) for v in (pedal, model, open_path))
        p_model_floor = p_model + floor
        ref_played = 10 ** (meta["ref_dbfs"] / 20)
        level_in = db(window_power(played[a:])) - 20 * np.log10(ref_played) + 3.0   # sine rms → peak, roughly
        open_db = db(p_open)
        jump = np.abs(np.diff(open_db, prepend=open_db[0])) > JUMP_DB
        jump = jump | np.r_[jump[1:], False] | np.r_[False, jump[:-1]]
        click = p_pedal > p_open * 10 ** (3 / 10)
        seen = (open_db > db(floor) + OVER_FLOOR_DB) & ~jump & ~click
        gain_pedal = db(p_pedal) - db(p_open)
        gain_model = db(p_model_floor) - db(p_open)
        d = gain_model - gain_pedal
        above = level_in >= THRESHOLD_DB[s["Threshold"]] + OPEN_DB
        energy = db(np.sum(p_model_floor) / np.sum(p_pedal))
        wobble = 20 * np.log10(np.abs(scipy.signal.sosfiltfilt(WOBBLE, raw[a:])).max() / np.abs(raw[a:]).max())
        stats = []
        for mask in (seen & above, seen & ~above):
            dd = np.abs(d[mask])
            stats.append((mask.sum() / max(seen.sum(), 1), np.median(d[mask]) if mask.any() else np.nan,
                          np.percentile(dd, 95) if mask.any() else np.nan,
                          np.mean(dd > 3) if mask.any() else np.nan))
        open_mask = seen & (gain_pedal > np.median(gain_pedal[seen & above]) - 1) if (seen & above).any() else seen
        idx = np.repeat(open_mask, WINDOW)[:len(pedal) - a]
        tone = third_octave(model[a:][:len(idx)][idx]) - third_octave(pedal[a:][:len(idx)][idx])
        rows.append((name, energy, stats, tone, wobble))
        traces[name] = (level_in, gain_pedal, gain_model, seen, THRESHOLD_DB[s["Threshold"]] + OPEN_DB)
        gain = 0.5 / max(np.abs(pedal[a:]).max(), np.abs(model[a:]).max())
        soundfile.write(OUT / f"{name}.pedal.wav", raw[a:] * gain, RATE, subtype="FLOAT")
        soundfile.write(OUT / f"{name}.model.wav", model[a:] * gain, RATE, subtype="FLOAT")
    np.savez_compressed("build-ns2/reamp-traces.npz", **{f"{k}.{i}": v for k, t in traces.items()
                                                for i, v in enumerate(t[:4])},
                        **{f"{k}.opens": t[4] for k, t in traces.items()})
    print("gain = output re the open path, 20 ms windows where that is 12 dB over the rig's floor (no level jump or click); "
          "d = model − pedal (dB)")
    print(f"{'take':34s} energy │ input over threshold: share  med d  p95|d|  >3dB │ under: share  med d  p95|d|  >3dB │ wobble")
    for name, energy, stats, _, wobble in rows:
        cells = "│".join(f" {sh:5.0%} {md:+6.1f} {p95:6.1f} {f3:5.0%} " for sh, md, p95, f3 in stats)
        print(f"{name:34s} {energy:+5.1f}  │{cells}│ {wobble:+5.1f}")
    print(f"\nopen-path tone, model − pedal (dB), windows where the pedal is open, at " + " ".join(f"{b:>5}" for b in BANDS))
    for name, _, _, tone, _ in rows:
        print(f"{name:34s} " + " ".join(f"{t:+5.1f}" for t in tone))


if __name__ == "__main__":
    main()
