#!/usr/bin/env python3
"""The RV-6's Time off noon, refitted on the focused session's bursts.

October's fits set each mode's RT60 table on the sweeps' responses at 1 kHz and the measured
start's level on its waveform; on the focused session's bursts the short settings came out long
in Hall (11-31 %), long in Room's lows and short in Plate's. Here, per mode and position, the
wet after the -12 dB burst is followed per octave band (250 Hz-8 kHz, 20 ms steps, both outputs'
mean energy, from the burst's end down to 6 dB over the pedal's floor; what is more than 30 dB
under a band's top counts less) and a few numbers are fitted to the pedal's levels over time,
NAM-X rendered from the same played signal (render_rv6) through the same chain:

  rt60       the tank's RT60 at 1 kHz at that position (Voicing::rt60_1k)
  energy     the wet's level correction there (Voicing::energy_db)
  early      the measured start's level there (EarlyKernel::gain_db; Plate, Hall, Room)
  slow       the slower tank's level there (DecayShape::slow_at; Hall, Room)
  from       where the measured start's extra decay begins (EarlyKernel::decay_from_ms): in no
             fit now. Scanned for Hall from 200 to 300 ms with the rest refitted each time, the
             levels are best at 200-225 ms: October's 200 stays

FITS says which per mode and position: 7:00 and 9:00 in Plate, Hall and Room, 9:00 in Modulate
(its 7:00 holds); in Hall, whose tank is fed after the swell since 2026-11-17, also the levels at
3:00 and 5:00. Spring holds as it is and is left.

  rv6/time.py            before and after per mode and position: the rms of the level differences
                         and the decay times (Schroeder, -5 to -25 dB) per octave
  rv6/time.py --write    the same, and the header's tables rewritten (then clang-format, and
                         rebuild build-rv6/render_rv6)

Run from the repository root, with build-rv6/render_rv6 built from the current header."""
import pathlib, re, subprocess, sys
import numpy as np, scipy.optimize, scipy.signal
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import fit_rv6, fit_rv6_decay, focused, scorecard, takes

RATE = takes.RATE
BANDS = [250, 500, 1000, 2000, 4000, 8000]
STEP = RATE // 50
INDEX = {"0700": 0, "0900": 1, "0300": 3, "0500": 4}
SHORT = ("rt", "energy", "early", "slow")
FITS = {
    "modulate": {"0900": ("rt", "energy")},
    "plate": {"0700": ("rt", "energy", "early"), "0900": ("rt", "energy", "early")},
    "hall": {"0700": SHORT, "0900": SHORT, "0300": ("energy", "early"), "0500": ("energy", "early")},
    "room": {"0700": SHORT, "0900": SHORT},
}
MODE_INDEX = {m.lower(): i for i, m in enumerate(takes.MODES)}
EARLY_DATA = pathlib.Path("src/dsp/Rv6EarlyData.h")
EARLY_ROW = re.compile(r"(EarlyKernel\{(\w+)_EARLY_A\.data\(\).*?48000\.0f,\s*)([-\d.]+)(f,\s*\{)([^{}]*)(\}\})", re.S)


def early_tables():
    """{mode: {"db": the measured start's level at the five Time positions, "from": where its
    extra decay begins, ms}} from Rv6EarlyData.h."""
    return {m.group(2).lower(): {"db": fit_rv6.numbers(m.group(5)), "from": float(m.group(3))}
            for m in EARLY_ROW.finditer(EARLY_DATA.read_text())}


def burst_span(name):
    """The take's -12 dB burst: its first sample and how long the wet is followed after it."""
    meta, *_ = takes.load(name)
    for part, start, body, tail in focused.parts_of(meta):
        if part["type"] == "noise-steps":
            return start, min(tail, 2 * RATE)
    raise ValueError(name)


def model_segment(name, lo, hi, overrides):
    """NAM-X's wet for the take's settings over played[lo:hi] (silence before), as takes.model_wet."""
    meta, played, *_ = takes.load(name)
    s = meta["settings"]
    args = [str(takes.MODES.index(s["Mode"]))] + [f"{takes.clock_fraction(s[k]):.4f}" for k in ("Time", "Tone", "E.Level")]
    fed = played[lo:hi]
    out = subprocess.run([takes.RENDER] + args + list(overrides), input=fed.astype(np.float32).tobytes(),
                         capture_output=True, check=True).stdout
    y = np.frombuffer(out, dtype=np.float32).reshape(-1, 2).astype(np.float64)
    ha, hb, offset = takes.dry_filters()
    return [scorecard.shift(takes.through(h, y[:, c] - fed), meta["offset"] - offset) for c, h in ((0, ha), (1, hb))]


def levels(pair, at, length):
    """Per octave band, the two outputs' mean power in 20 ms steps from `at`: bands x steps."""
    rows = []
    for centre in BANDS:
        power = 0.0
        for x in pair:
            y = focused.octave(x[at - RATE // 4:at + length], centre)[RATE // 4:]
            n = len(y) // STEP
            power = power + np.mean(y[:n * STEP].reshape(n, STEP) ** 2, axis=1)
        rows.append(0.5 * power)
    return np.array(rows)


def db(power):
    return 10 * np.log10(power + 1e-30)


def decay_times(pair, at, length, floors):
    """RT60 per octave band from the burst's end (Schroeder, a line from -5 to -25 dB), output A."""
    out = []
    for centre, floor in zip(BANDS, floors):
        energy = focused.octave(pair[0][at - RATE // 4:at + length], centre)[RATE // 4 + RATE // 20:] ** 2
        level = np.convolve(energy, np.ones(STEP) / STEP, "same")
        under = np.nonzero(level[STEP:] < floor * 10 ** 0.8)[0]
        end = under[0] + STEP if len(under) else len(energy)
        curve = np.cumsum((energy[:end] - floor)[::-1])[::-1]
        curve = 10 * np.log10(np.maximum(curve, 1e-30) / max(curve[0], 1e-30))
        first, last = np.argmax(curve < -5), np.argmax(curve < -25)
        out.append(float(-60 / np.polyfit(np.arange(first, last) / RATE, curve[first:last], 1)[0]) if last > first else np.nan)
    return np.array(out)


class Target:
    """One take's burst: the pedal's levels over time and what of them stands over its floor."""

    def __init__(self, name):
        self.name = name
        self.at, self.length = burst_span(name)
        self.lo = self.at - RATE // 2
        pedal = takes.wet(name)
        self.pedal = levels(pedal, self.at, self.length)
        quiet = [x[self.at - RATE // 2:self.at - RATE // 20] for x in pedal]
        self.floors = np.array([np.mean(focused.octave(quiet[0], c)[RATE // 10:] ** 2) for c in BANDS])
        # Compared where the pedal stands 6 dB over its floor, after the burst itself (50 ms: the
        # dry's leftovers are in it), the pedal's floor put under the model.
        self.keep = self.pedal > 4 * self.floors[:, None]
        self.keep[:, :3] = False
        self.weight = np.where(self.pedal > 1e-3 * self.pedal[:, 3:].max(axis=1)[:, None], 1.0, 0.4)
        self.pedal_rt = decay_times(pedal, self.at, self.length, self.floors)

    def model(self, overrides):
        """The levels' differences, dB (bands x steps), and the model's decay times."""
        pair = model_segment(self.name, self.lo, self.at + self.length + RATE // 4, overrides)
        at = self.at - self.lo
        power = levels(pair, at, self.length)
        return db(power + self.floors[:, None]) - db(self.pedal), decay_times(pair, at, self.length, np.zeros(len(BANDS)))

    def error(self, overrides):
        return (self.model(overrides)[0] * self.weight)[self.keep]


def overrides_of(voicing, early, decay):
    out = ["rt60=" + ",".join(f"{v:.6g}" for v in voicing["rt"]), "energy=" + ",".join(f"{v:.6g}" for v in voicing["energy"])]
    if early is not None:
        out += ["early_db=" + ",".join(f"{v:.6g}" for v in early["db"]), f"early_from={early['from']:.6g}"]
    if decay["slow_gain"] > 0:
        out.append("decay.slow_at=" + ",".join(f"{v:.6g}" for v in decay["slow_at"]))
    return out


BOUNDS = {"rt": (np.log(0.2), np.log(20.0)), "energy": (-20.0, 12.0), "early": (-20.0, 12.0), "slow": (0.0, 3.0), "from": (100.0, 400.0)}
SCALES = {"rt": 0.1, "energy": 1.0, "early": 1.0, "slow": 0.3, "from": 30.0}


def fit_position(index, which, target, voicing, early, decay):
    """The position's numbers named in `which`, fitted to the take's levels."""
    def apply(p):
        v = dict(voicing, rt=list(voicing["rt"]), energy=list(voicing["energy"]))
        e = dict(early, db=list(early["db"])) if early is not None else None
        d = dict(decay, slow_at=list(decay["slow_at"]))
        for name, value in zip(which, p):
            if name == "rt":
                v["rt"][index] = float(np.exp(value))
            elif name == "energy":
                v["energy"][index] = float(value)
            elif name == "early":
                e["db"][index] = float(value)
            elif name == "slow":
                d["slow_at"][index] = float(value)
            elif name == "from":
                e["from"] = float(value)
        return v, e, d

    start = {"rt": np.log(voicing["rt"][index]), "energy": voicing["energy"][index],
             "early": early["db"][index] if early else 0.0, "slow": decay["slow_at"][index], "from": early["from"] if early else 0.0}
    p0 = [float(np.clip(start[name], *BOUNDS[name])) for name in which]
    fit = scipy.optimize.least_squares(lambda p: target.error(overrides_of(*apply(p))), p0,
                                       bounds=([BOUNDS[n][0] for n in which], [BOUNDS[n][1] for n in which]),
                                       diff_step=0.02, x_scale=[SCALES[n] for n in which], max_nfev=80)
    return apply(fit.x)


def rms(x):
    return float(np.sqrt(np.mean(np.square(x))))


def cells(values, form="{:5.2f}"):
    return " ".join("    -" if np.isnan(v) else form.format(v) for v in values)


def main():
    write = "--write" in sys.argv
    _, _, _, voicings = fit_rv6.read_block()
    decays = fit_rv6_decay.read_decays()
    early = early_tables()
    takes.all_lags()
    takes.dry_filters()
    print("per mode and Time: the levels' rms difference (dB) and RT60 per octave " + "/".join(str(b) for b in BANDS) + " Hz, s")
    for mode, positions in FITS.items():
        m = MODE_INDEX[mode]
        for position, which in positions.items():
            index = INDEX[position]
            target = Target(f"time-{position}-{mode}")
            before = overrides_of(voicings[m], early.get(mode), decays[m])
            v, e, d = fit_position(index, which, target, voicings[m], early.get(mode), decays[m])
            after = overrides_of(v, e, d)
            print(f"{mode:9s} {position}  pedal RT      {cells(target.pedal_rt)}")
            for label, args in (("before", before), ("after", after)):
                difference, rt = target.model(args)
                print(f"{'':15s}{label:6s} RT      {cells(rt)}   levels rms {rms(difference[target.keep]):.2f} dB, "
                      f"mean {np.mean(difference[target.keep]):+.2f}")
            notes = []
            if "rt" in which:
                notes.append(f"rt60 {voicings[m]['rt'][index]:.4g} -> {v['rt'][index]:.4g} s")
            if "energy" in which:
                notes.append(f"energy {voicings[m]['energy'][index]:+.2f} -> {v['energy'][index]:+.2f} dB")
            if "early" in which:
                notes.append(f"early {early[mode]['db'][index]:+.2f} -> {e['db'][index]:+.2f} dB")
            if "slow" in which:
                notes.append(f"slow x{decays[m]['slow_at'][index]:.2f} -> x{d['slow_at'][index]:.2f}")
            if "from" in which:
                notes.append(f"the start's decay from {early[mode]['from']:.0f} -> {e['from']:.0f} ms")
            print(f"{'':15s}" + ", ".join(notes))
            # The other takes at this Time, in no fit: Tone at its ends.
            for other in (f"tone-0500-time-{position}-{mode}", f"tone-0700-time-{position}-{mode}"):
                if other in takes.names():
                    check = Target(other)
                    print(f"{'':15s}check {other}: levels rms {rms(check.model(before)[0][check.keep]):.2f} -> {rms(check.model(after)[0][check.keep]):.2f} dB")
            voicings[m], decays[m] = v, d
            if e is not None:
                early[mode] = e
    if write:
        text, begin, end, _ = fit_rv6.read_block()
        fit_rv6.write_block(text, begin, end, voicings)
        fit_rv6_decay.write_decays(decays)
        EARLY_DATA.write_text(EARLY_ROW.sub(
            lambda r: r.group(1) + fit_rv6.f(early[r.group(2).lower()]["from"])[:-1] + r.group(4)
            + ", ".join(fit_rv6.f(v) for v in early[r.group(2).lower()]["db"]) + r.group(6), EARLY_DATA.read_text()))
        print("wrote the tables: clang-format the two headers and rebuild build-rv6/render_rv6")


if __name__ == "__main__":
    main()
