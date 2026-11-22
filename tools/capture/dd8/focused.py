"""The DD-8 focused reamp (mono, OUTPUT A) against NAM-X's Delay (the DD-8 models): NAM-X's own
processor rendered from each take's played signal (render_dd8) — its type the take's mode, its
time the delay measured on the take, Mix and Feedback at the pedal's Level and Feedback positions
— through the bypass take's chain, against the pedal.

The marker chirp went through the pedal in this session (and through the model's render: the
played file has it), and it is the cleanest probe in each take: 50 ms, 200 Hz-10 kHz, at the
reference level, half a second before the guitar. Per take:

  time      the delay: the first repeat's arrival after the dry (the marker's matched filter)
  repeat 1  the first repeat's level re the dry, pedal and model (the Level law and the mode's
            own loss), and its tone: its level in four bands re the dry's, model - pedal
  decay     dB per repeat while the take rings out after the guitar stops (the Feedback law)
  score     the scorecard over the whole take (third-octave bands, 50 ms windows)

  dd8/focused.py [SESSION]   (default captures/boss/dd-8/2026-11-09-focused)

Run from the repository root, with build-dd8/render_dd8 built (the command in
tools/capture/render_dd8.cpp's header)."""
import json, os, pathlib, subprocess, sys
import numpy as np, scipy.signal
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import scorecard

SESSION = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "captures/boss/dd-8/2026-11-09-focused")
RATE = scorecard.RATE
REF_NAMX_DBFS = -6.0
RENDER = os.environ.get("RENDER_DD8", "build-dd8/render_dd8")  # another build of it, to compare
TYPES = {"Standard": 1, "Analog": 2, "Tape": 3, "Warm": 4, "Mod": 5}
POSITION = {"7:00": 0.0, "9:00": 0.2, "12:00": 0.5, "3:00": 0.8, "5:00": 1.0}
MARKER = int(0.05 * RATE)
TONE_BANDS = [(300, 1000), (1000, 2500), (2500, 5000), (5000, 9000)]


def db(power):
    return 10 * np.log10(power + 1e-30)


def arrivals(x, played, meta, search_ms=(60, 900)):
    """The marker's matched filter over x: the dry's peak and the first repeat's (sample, value)."""
    m0 = meta["marker_start"]
    template = played[m0:m0 + MARKER]
    span = x[m0 - 240:m0 + int(1.0 * RATE)]
    c = np.abs(scipy.signal.hilbert(scipy.signal.correlate(span, template, "valid")))
    dry = int(np.argmax(c[:480]))
    lo, hi = dry + int(search_ms[0] * RATE / 1000), dry + int(search_ms[1] * RATE / 1000)
    first = lo + int(np.argmax(c[lo:hi]))
    return dry + m0 - 240, c[dry], first + m0 - 240, c[first]


def band_energy(x, start):
    seg = x[start:start + MARKER + 480] * 1.0
    f, p = scipy.signal.periodogram(seg, RATE)
    return np.array([p[(f >= lo) & (f < hi)].sum() for lo, hi in TONE_BANDS])


def decay_per_repeat(x, start, period, floor):
    """dB lost per repeat after `start` (the guitar has stopped): a line through the level of
    whole periods, as long as they stay 10 dB over the floor."""
    levels = []
    for i in range(start, len(x) - period, period):
        level = db(np.mean(x[i:i + period] ** 2))
        if level < floor + 10:
            break
        levels.append(level)
    if len(levels) < 3:
        return float("nan"), len(levels)
    return float(np.polyfit(np.arange(len(levels)), levels, 1)[0]), len(levels)


def render(played, meta, time_ms):
    s = meta["settings"]
    scale = 10 ** ((REF_NAMX_DBFS - meta["ref_dbfs"]) / 20)
    y = subprocess.run([RENDER, str(TYPES[s["Mode"]]), f"{time_ms:.4f}", str(POSITION[s["Level"]]),
                        str(POSITION[s["Feedback"]])], input=(played * scale).astype(np.float32).tobytes(),
                       capture_output=True, check=True).stdout
    return np.frombuffer(y, dtype=np.float32).astype(np.float64) / scale


def main():
    _, played, pedal, _ = scorecard.load(SESSION, "bypass-stabs")
    h = scorecard.chain_filter(played, pedal)
    takes = sorted((json.loads(p.read_text()) for p in SESSION.glob("*.json") if p.name != "rig.json"),
                   key=lambda m: m["recorded_at"])
    rows, scores = [], []
    for meta in takes:
        if meta["bypass"]:
            continue
        name = meta["take"]
        meta, played, pedal, pre = scorecard.load(SESSION, name)
        pedal = pedal - np.mean(pre)
        dry_at, dry, first_at, first = arrivals(pedal, played, meta)
        time_ms = (first_at - dry_at) / RATE * 1000
        model = scorecard.through(h, render(played, meta, time_ms))
        m_dry_at, m_dry, m_first_at, m_first = arrivals(model, played, meta)
        tone = db(band_energy(model, m_first_at) / band_energy(model, m_dry_at)) - \
            db(band_energy(pedal, first_at) / band_energy(pedal, dry_at))
        body_end = meta["signal_start"] + int(meta["signal"]["seconds"] * RATE) + RATE // 20
        period = first_at - dry_at
        floor = db(np.mean((pre[RATE // 10:] - np.mean(pre)) ** 2))
        pedal_decay, count = decay_per_repeat(pedal, body_end, period, floor)
        model_decay, _ = decay_per_repeat(model, body_end, period, floor)
        rows.append((name, time_ms, (m_first_at - m_dry_at) / RATE * 1000, 20 * np.log10(first / dry),
                     20 * np.log10(m_first / m_dry), tone, pedal_decay, model_decay, count))
        scores.append((name, scorecard.score(pedal, model, pre, meta["signal_start"], time_invariant=False)))
    print(f"{'take':30s} time ms (model) │ repeat 1 re dry: pedal  model │ its tone, model − pedal, dB: "
          + "  ".join(f"{lo / 1000:g}-{hi / 1000:g}k" for lo, hi in TONE_BANDS) + " │ dB per repeat: pedal  model (repeats)")
    for name, t, mt, lv, mlv, tone, pd, md, count in rows:
        print(f"{name:30s} {t:7.1f} ({mt:6.1f}) │        {lv:+6.1f} {mlv:+6.1f}        │      "
              + "  ".join(f"{v:+6.1f}" for v in tone) + f"          │      {pd:+6.2f} {md:+6.2f}  ({count})")
    print()
    print(scorecard.HEADER)
    for name, s in scores:
        print(scorecard.row(name, s))
    print()
    print(scorecard.tone_rows(scores, step=2))


if __name__ == "__main__":
    main()
