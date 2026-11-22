"""The RV-6 focused session's takes, ready to compare: both outputs aligned to the played signal
with the recording's one-sample slips undone, the dry taken out (the wet alone), and NAM-X's
Reverb rendered from the same played signal (render_rv6) through the same chain.

Slips. PipeWire now and then repeats a sample in the middle of a take (about one take in four
here: they are long); all three recorded channels slip together, so the loopback shows them.
`scorecard.slips` can't read this session: along a sweep the loopback's delay moves with frequency
(about a sample between 300 Hz and 3 kHz, several under 100 Hz), which it takes for a slip in
nearly every take. Here every quarter-second block's lag (300 Hz-3 kHz) is compared with the
median of all the takes' lags at that block — the takes share their sweeps and bursts — and a
slip is a whole-sample step off that median that stays; it is then placed to the sample (where
the loopback stops matching the played signal at the old lag and starts matching at the new one).

The dry. With E.LEVEL fully down (`level-0700-hall`) the pedal's outputs carry the dry alone:
that take's played -> recorded response per output is the dry path — its sweep deconvolved, a
4096-tap FIR (`scorecard.chain_filter`'s Welch estimate misreads a sweep: it left the dry 10 dB
down where this leaves it 55-60 dB down, on the take's guitar too) — and a take's wet is its
recording minus the played signal through it, moved by the take's own sub-sample offset (read on
the loopback). The model's wet goes through the same FIR.
"""
import functools, json, os, pathlib, subprocess, sys
import numpy as np, scipy.signal, soundfile
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import scorecard

RATE = scorecard.RATE
SESSION = pathlib.Path(os.environ.get("RV6_SESSION", "captures/boss/rv-6/2026-11-15-focused"))
CACHE = pathlib.Path("build-rv6")
RENDER = os.environ.get("RENDER_RV6", "build-rv6/render_rv6")
MODES = ["Modulate", "Spring", "Plate", "Hall", "Room", "Shimmer"]  # render_rv6's order
DRY_TAKE = "level-0700-hall"
BLOCK = RATE // 4
SEARCH = 8
LAG_BAND = scipy.signal.butter(4, [300, 3000], "bandpass", fs=RATE, output="sos")


def clock_fraction(position):
    """A clock string on the knob's 300 degree turn: 7:00 = 0, 12:00 = 0.5, 5:00 = 1."""
    hour, minute = map(int, position.split(":"))
    return ((hour - 7) % 12 + minute / 60) / 10


def names():
    return sorted(p.stem for p in SESSION.glob("*.json") if p.stem != "rig")


def raw(name):
    meta = json.loads((SESSION / f"{name}.json").read_text())
    recorded, _ = soundfile.read(SESSION / f"{name}.wav", dtype="float64")
    played, _ = soundfile.read(SESSION / "played" / f"{name}.wav", dtype="float64")
    return meta, played, recorded


def block_lags(loop, played, start):
    """{block start: the loopback's lag re the played signal there, samples} for the blocks loud
    enough between 300 Hz and 3 kHz."""
    loop, played = scipy.signal.sosfiltfilt(LAG_BAND, loop), scipy.signal.sosfiltfilt(LAG_BAND, played)
    out = {}
    for i in range(start, len(played) - BLOCK - SEARCH, BLOCK):
        x = played[i + SEARCH:i + BLOCK - SEARCH]
        if np.sqrt(np.mean(x ** 2)) < 1e-4:
            continue
        c = [np.dot(x, loop[i + SEARCH + k:i + BLOCK - SEARCH + k]) for k in range(-SEARCH, SEARCH + 1)]
        k = int(np.argmax(c))
        if 0 < k < 2 * SEARCH:
            out[i] = k - SEARCH + 0.5 * (c[k - 1] - c[k + 1]) / (c[k - 1] - 2 * c[k] + c[k + 1])
    return out


@functools.cache
def all_lags():
    """Every take's block lags (cached in build-rv6/lags.json: reading 123 takes takes minutes)."""
    path = CACHE / f"lags-{SESSION.name}.json"
    if path.exists():
        return {name: {int(i): v for i, v in d.items()} for name, d in json.loads(path.read_text()).items()}
    out = {}
    for name in names():
        meta, played, recorded = raw(name)
        lat = meta["latency_samples"]
        n = min(len(played), len(recorded) - lat) - 16
        out[name] = block_lags(recorded[lat:lat + n + 16, 1], played[:n + 16], meta["signal_start"])
    CACHE.mkdir(exist_ok=True)
    path.write_text(json.dumps(out))
    return out


@functools.cache
def reference_lags():
    """Per block start, the median over the takes of the lag's part that isn't a whole-sample
    step (blocks at least 8 takes have)."""
    at = {}
    for lags in all_lags().values():
        for i, v in lags.items():
            at.setdefault(i, []).append(v)
    return {i: float(np.median(np.array(v) - np.round(np.array(v) - np.median(v))))
            for i, v in at.items() if len(v) >= 8}


PLACE_BAND = scipy.signal.butter(4, 200, "highpass", fs=RATE, output="sos")


def place(loop, played, lat, lo, hi, before, after):
    """The sample between `lo` and `hi` where the loopback's lag goes from `before` to `after`
    (whole samples): the split that leaves the least error, over everything above 200 Hz — a slip
    in the sweep's last seconds (over 3 kHz: no lag is read there) is found by the sweep itself;
    where nothing plays, any place is as good."""
    p = scipy.signal.sosfiltfilt(PLACE_BAND, played[lo:hi])
    errors = []
    for lag in (before, after):
        seg = scipy.signal.sosfiltfilt(PLACE_BAND, loop[lat + lo + lag:lat + hi + lag])
        errors.append((seg - p * np.dot(seg, p) / np.dot(p, p)) ** 2)
    cost = np.concatenate([[0], np.cumsum(errors[0])]) + (errors[1].sum() - np.concatenate([[0], np.cumsum(errors[1])]))
    return lo + int(np.argmin(cost))


def slips_of(name, meta, played, recorded):
    """[(sample, whole samples slipped from there on)] and the take's sub-sample offset re the
    session's median."""
    reference = reference_lags()
    seq = [(i, v - reference[i]) for i, v in sorted(all_lags()[name].items()) if i in reference]
    if not seq:
        return [], 0.0
    steps = np.round([v for _, v in seq]).astype(int)
    fraction = float(np.median([v - s for (_, v), s in zip(seq, steps)]))
    found, current = [], 0
    lat = meta["latency_samples"]
    if steps[0] != 0:
        found.append((0, int(steps[0])))
        current = int(steps[0])
    for j in range(1, len(seq)):
        if steps[j] != current and (j + 1 == len(seq) or steps[j + 1] == steps[j]):
            found.append((place(recorded[:, 1], played, lat, seq[j - 1][0], seq[j][0] + BLOCK, current, int(steps[j])),
                          int(steps[j])))
            current = int(steps[j])
    # A step that is back within half a second is a block misread (a guitar's quiet stretch), not a slip.
    kept, level = [], 0
    for k, (i, step) in enumerate(found):
        if k + 1 < len(found) and found[k + 1][1] == level and found[k + 1][0] - i <= 2 * BLOCK:
            continue
        if kept and step == level:
            continue
        if step != level:
            kept.append((i, step))
            level = step
    return kept, fraction


@functools.cache
def load(name):
    """meta (with "slips" and "offset"), the played signal, outputs A and B aligned to it (slips
    undone), the pedal's A before the marker."""
    meta, played, recorded = raw(name)
    lat = meta["latency_samples"]
    n = min(len(played), len(recorded) - lat) - 16
    meta["slips"], meta["offset"] = slips_of(name, meta, played, recorded)
    outputs = []
    for channel in (0, 2):
        x = recorded[lat:lat + n, channel].copy()
        for i, step in meta["slips"]:
            x[i:] = recorded[lat + i + step:lat + n + step, channel]
        outputs.append(x)
    return meta, played[:n], outputs[0], outputs[1], recorded[:meta["marker_start"], 0]


HALF = 2048                       # the dry path's FIR: 4096 taps, centred


def sweep_response(played, recorded, start, seconds):
    """played -> recorded over the take's sweep (it starts the signal), as a centred FIR."""
    n, size = int(seconds * RATE), 1 << 20
    p = np.fft.rfft(played[start:start + n], size)
    x = recorded[start:start + n]
    h = np.fft.irfft(np.fft.rfft(x - x.mean(), size) * np.conj(p) / (np.abs(p) ** 2 + 1e-9 * np.max(np.abs(p) ** 2)))
    return np.roll(h, HALF)[:2 * HALF] * np.hanning(2 * HALF)


def through(h, x):
    return scipy.signal.fftconvolve(x, h)[HALF:HALF + len(x)]


@functools.cache
def dry_filters():
    meta, played, a, b, _ = load(DRY_TAKE)
    sweep = meta["signal"]["parts"][0]
    seconds = sweep["seconds"] + sweep["tail"] - 1
    return (sweep_response(played, a, meta["signal_start"], seconds),
            sweep_response(played, b, meta["signal_start"], seconds), meta["offset"])


def dry_of(name):
    """The dry as the pedal's two outputs carry it in this take."""
    meta, played, *_ = load(name)
    ha, hb, offset = dry_filters()
    return [scorecard.shift(through(h, played), meta["offset"] - offset) for h in (ha, hb)]


def wet(name):
    """The pedal's wet alone, outputs A and B."""
    _, _, a, b, _ = load(name)
    da, db_ = dry_of(name)
    return a - da, b - db_


def model_wet(name, overrides=()):
    """NAM-X's wet for this take's settings and played signal, outputs A and B, through the dry
    path's chain (the rig's response) and at the take's sub-sample offset."""
    meta, played, *_ = load(name)
    s = meta["settings"]
    args = [str(MODES.index(s["Mode"]))] + [f"{clock_fraction(s[k]):.4f}" for k in ("Time", "Tone", "E.Level")]
    fed = played.copy()
    fed[:meta["signal_start"]] = 0.0  # the marker went to the loopback only: the pedal never heard it
    out = subprocess.run([RENDER] + args + list(overrides), input=fed.astype(np.float32).tobytes(),
                         capture_output=True, check=True).stdout
    y = np.frombuffer(out, dtype=np.float32).reshape(-1, 2).astype(np.float64)
    ha, hb, offset = dry_filters()
    return [scorecard.shift(through(h, y[:, c] - fed), meta["offset"] - offset)
            for c, h in ((0, ha), (1, hb))]
