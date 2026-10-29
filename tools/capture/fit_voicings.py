#!/usr/bin/env python3
"""Fits the filter chains of NAM-X's pedal types to the studies' measurements and prints
them as C++ constants, so every chain in the src/dsp/*.h pedal headers can be reproduced.
Written 2026-10-29, when the bypass correction (analyze_dd8.head) moved the measurements
these chains were first fitted to (by hand, with scripts that weren't kept).

    fit_voicings.py            # every fit below; reads the studies' fit JSONs, and the BF-3
                               # session's WAVs for its fine wet tone

The chains are RBJ biquads exactly as BiquadChain.h designs them, at 48 kHz, read the way
the studies measure: power averaged over each band (a third octave for the DD-8, ±6 %
for the CS-3 and NS-2, a twelfth of an octave either side for the BF-3's fine curve);
evaluating band centres instead misread steep slopes by up to 4 dB. Each fit starts from
the header's current values, frees what the header fitted, and reports the rms error (dB)
of the old chain and the new one against the corrected measurement. (Checked: against the
old measurements the old chains come out as their headers say, e.g. DD-8 Standard 0.06 dB.)
"""

import json
import pathlib

import numpy as np
import scipy.optimize

import analyze_bf3

RATE = 48000
THIRD = 2 ** (1 / 6)  # analysis.third_octave_db's band edges
SIX_PERCENT = 1.06    # analyze_cs3 / analyze_ns2 band edges
REPOSITORY = pathlib.Path(__file__).resolve().parents[2]
BOSS = REPOSITORY / "captures" / "boss"


def biquad_db(kind, hz, q, gain_db, f):
    """|H| in dB of one BiquadChain.h section at frequencies f."""
    if kind == "none" or hz >= 0.49 * RATE:
        return np.zeros_like(f)
    w = 2 * np.pi * hz / RATE
    c, alpha, a = np.cos(w), np.sin(w) / (2 * q), 10 ** (gain_db / 40)
    if kind == "low_pass":
        b, d = [(1 - c) / 2, 1 - c, (1 - c) / 2], [1 + alpha, -2 * c, 1 - alpha]
    elif kind == "high_pass":
        b, d = [(1 + c) / 2, -(1 + c), (1 + c) / 2], [1 + alpha, -2 * c, 1 - alpha]
    elif kind == "peak":
        b, d = [1 + alpha * a, -2 * c, 1 - alpha * a], [1 + alpha / a, -2 * c, 1 - alpha / a]
    elif kind == "low_shelf":
        root = 2 * np.sqrt(a) * alpha
        b = [a * ((a + 1) - (a - 1) * c + root), 2 * a * ((a - 1) - (a + 1) * c),
             a * ((a + 1) - (a - 1) * c - root)]
        d = [(a + 1) + (a - 1) * c + root, -2 * ((a - 1) + (a + 1) * c),
             (a + 1) + (a - 1) * c - root]
    else:
        raise ValueError(kind)
    z = np.exp(-1j * 2 * np.pi * f / RATE)
    h = (b[0] + b[1] * z + b[2] * z ** 2) / (d[0] + d[1] * z + d[2] * z ** 2)
    return 20 * np.log10(np.abs(h))


def chain_db(sections, f):
    return sum(biquad_db(*s, f) for s in sections)


def band_db(sections, centres, edge):
    """The chain's power averaged over each band [c / edge, c · edge], in dB."""
    out = []
    for c in centres:
        f = np.geomspace(c / edge, c * edge, 48)
        out.append(10 * np.log10(np.mean(10 ** (chain_db(sections, f) / 10))))
    return np.array(out)



class Chain:
    """Sections [kind, hz, q, gain_db] plus which of hz/q/gain are free, per section."""

    def __init__(self, sections, free):
        self.sections = [list(s) for s in sections]
        self.free = free  # e.g. [("hz", "q"), (), ("hz", "q", "gain")]

    def vector(self):
        out = []
        for s, names in zip(self.sections, self.free):
            for name in names:
                out.append({"hz": np.log(s[1]), "q": np.log(s[2]), "gain": s[3]}[name])
        return np.array(out)

    def with_vector(self, v):
        sections, k = [list(s) for s in self.sections], 0
        for s, names in zip(sections, self.free):
            for name in names:
                if name == "hz":
                    s[1] = float(np.exp(v[k]))
                elif name == "q":
                    s[2] = float(np.exp(v[k]))
                else:
                    s[3] = float(v[k])
                k += 1
        return sections


def rms(x):
    return float(np.sqrt(np.mean(np.square(x))))


def fit_chain(name, chain, f, target, edge, normalize_hz=None, q_max=4.0):
    """Least squares on dB over bands; with normalize_hz, re the chain's band there."""
    def model(sections):
        m = band_db(sections, f, edge)
        if normalize_hz is not None:
            m = m - band_db(sections, np.array([float(normalize_hz)]), edge)[0]
        return m

    def residual(v):
        return model(chain.with_vector(v)) - target

    v0 = chain.vector()
    lower, upper = [], []
    for s, names in zip(chain.sections, chain.free):
        for n in names:
            lower.append({"hz": np.log(20.0), "q": np.log(0.3), "gain": -24.0}[n])
            upper.append({"hz": np.log(0.45 * RATE), "q": np.log(q_max), "gain": 24.0}[n])
    v0 = np.clip(v0, np.array(lower) + 1e-9, np.array(upper) - 1e-9)
    result = scipy.optimize.least_squares(residual, v0, bounds=(lower, upper))
    new = chain.with_vector(result.x)
    print(f"\n== {name}: rms {rms(model(chain.sections) - target):.2f} dB (old chain) -> "
          f"{rms(model(new) - target):.2f} dB (refitted), {len(f)} points "
          f"{f.min():.0f}-{f.max():.0f} Hz")
    for s in new:
        kind = s[0].upper()
        print(f"    {{FilterKind::{kind}, {s[1]:.1f}f, {s[2]:.3f}f, {s[3]:.2f}f}},")
    return new


def load(path):
    with open(path, encoding="utf-8") as file:
        return json.load(file)


def dd8():
    third = np.array([31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000])
    keep = third >= 31.5
    sessions = {"standard": "2026-10-26-standard/fit/dd-8-standard.json",
                "analog": "2026-10-26-analog/fit/dd-8-analog.json",
                "tape": "2026-10-26-tape/fit/dd-8-tape.json",
                "warm": "2026-10-26-warm/fit/dd-8-warm.json"}
    tone = {}
    for k, p in sessions.items():
        d = load(BOSS / "dd-8" / p)
        tone[k] = np.array(d["wet_tone_db_re_1khz"]["db"] if k == "standard" else d["tone"]["db_re_1khz"])
    # Standard's out chain (Mod uses it too)
    fit_chain("DD-8 STANDARD_OUT", Chain(
        [["low_pass", 6248.9, 0.721, 0], ["low_pass", 11118.6, 2.5, 0], ["peak", 60.3, 0.917, 1.61]],
        [("hz", "q"), ("hz", "q"), ("hz", "q", "gain")]), third[keep], tone["standard"][keep], THIRD, 1000)
    # Analog: a 6th-order Butterworth in the loop, set by hand at 2780 Hz to match the 4 kHz
    # point (it can't peak, so it can sit in the loop). Not refitted: the corner that puts
    # the corrected 4 kHz band (-19.5 dB, was -19.0) on the measurement costs 9 dB at 8 kHz.
    def butterworth(hz):
        return [["low_pass", hz, 0.5176, 0], ["low_pass", hz, 0.7071, 0], ["low_pass", hz, 1.9319, 0]]
    model = (band_db(butterworth(2780), third, THIRD)
             - band_db(butterworth(2780), np.array([1000.0]), THIRD)[0])
    print("\n== DD-8 ANALOG_LOOP (kept, 2780 Hz): model - measured at 2 / 4 / 8 kHz: "
          + " / ".join(f"{model[k] - tone['analog'][k]:+.1f}" for k in (6, 7, 8)) + " dB")
    fit_chain("DD-8 TAPE out", Chain(
        [["high_pass", 62.5, 0.4, 0], ["low_pass", 12809.8, 1.437, 0], ["low_pass", 3510.1, 1.023, 0],
         ["peak", 320.5, 0.867, 3.57]],
        [("hz", "q"), ("hz", "q"), ("hz", "q"), ("hz", "q", "gain")]), third[keep], tone["tape"][keep], THIRD, 1000)
    fit_chain("DD-8 WARM out", Chain(
        [["low_shelf", 504.3, 0.429, 3.45], ["low_pass", 7168.2, 0.598, 0], ["low_pass", 13778.6, 2.5, 0]],
        [("hz", "q", "gain"), ("hz", "q"), ("hz", "q")]), third[keep], tone["warm"][keep], THIRD, 1000)
    # Tape's wow and flutter as delay swings: a sine of rms c cents at f Hz is a delay sine
    # of peak c·√2 / (1200/ln 2) / (2π f) seconds.
    d = load(BOSS / "dd-8" / sessions["tape"])["wow_flutter"]
    for label, key, hz in (("lfo_ms (wow)", "wow (0.3-6 Hz)", 0.53), ("flutter_ms", "flutter (6-60 Hz)", 6.7)):
        cents = d[key]["rms_cents"]
        ms = cents * np.sqrt(2) / (1200 / np.log(2)) / (2 * np.pi * hz) * 1000
        print(f"\n== DD-8 TAPE {label}: {cents} cents rms at {hz} Hz -> {ms:.4f} ms")


def bf3():
    static = analyze_bf3.Static(BOSS / "bf-3" / "2026-10-27-standard")
    power = np.abs(static.wet_tone) ** 2
    centres = 1000 * 2 ** (np.arange(-24, 25) / 6)  # 1/6 octave
    centres = centres[(centres >= 90) & (centres <= 18000)]
    edge = 2 ** (1 / 12)
    level = np.array([10 * np.log10(np.mean(power[(analyze_bf3.FREQS >= c / edge)
                                                  & (analyze_bf3.FREQS < c * edge)])) for c in centres])
    level -= np.interp(np.log(1000), np.log(centres), level)
    fit_chain("BF-3 TONE", Chain(
        [["high_pass", 191.8, 0.519, 0], ["high_pass", 94.0, 1.556, 0], ["low_pass", 6293.0, 0.542, 0],
         ["low_pass", 17471.7, 0.923, 0]],
        [("hz", "q"), ("hz", "q"), ("hz", "q"), ("hz", "q")]), centres, level, edge, 1000)


def cs3():
    """Tone: per position the response re its own 1 kHz (the chain is normalized there in
    Cs3Compressor.h); the 38 Hz low cut and 18 kHz roll-off stay fixed (freeing them only
    drives them to the bounds for 0.07 dB), the two shelves' corners and Qs are shared, their
    gains per position; TONE_GAIN_DB is the 1 kHz level re 12:00, read from the measurement."""
    tone = load(BOSS / "cs-3" / "2026-10-27" / "fit" / "cs-3.json")["tone"]
    f = np.array(tone["hz"], dtype=float)
    positions = ["7:00", "9:00", "12:00", "3:00", "5:00"]
    target = {p: np.array(tone[p]["db_re_1khz"]) for p in positions}
    at_1k = tone["hz"].index(1000)
    gain = [tone[p]["db_re_bypass"][at_1k] - tone["12:00"]["db_re_bypass"][at_1k] for p in positions]

    def sections(shared, low, high):
        hp_hz, hp_q, lp_hz, lp_q, sl_hz, sl_q, sh_hz, sh_q = shared
        return [["high_pass", hp_hz, hp_q, 0], ["low_pass", lp_hz, lp_q, 0],
                ["low_shelf", sl_hz, sl_q, low], ["low_shelf", sh_hz, sh_q, high]]

    def model(shared, low, high):
        ch = sections(shared, low, high)
        return band_db(ch, f, SIX_PERCENT) - band_db(ch, np.array([1000.0]), SIX_PERCENT)[0]

    def unpack(v):
        return np.concatenate([[38.3, 0.467, 18000.0, 0.482], np.exp(v[:4])]), v[4:9], v[9:14]

    def residual(v):
        shared, low, high = unpack(v)
        return np.concatenate([model(shared, low[k], high[k]) - target[p]
                               for k, p in enumerate(positions)])

    v0 = np.concatenate([np.log([649.6, 0.482, 7195.8, 0.596]), [9.35, 6.10, 0.08, -5.74, -9.60],
                         [4.25, 2.69, 4.15, 4.43, 2.84]])
    result = scipy.optimize.least_squares(residual, v0)
    shared, low, high = unpack(result.x)
    per = [rms(model(shared, low[k], high[k]) - target[p]) for k, p in enumerate(positions)]
    print(f"\n== CS-3 tone (shapes re 1 kHz, 5 positions x {len(f)} bands): rms "
          f"{rms(residual(v0)):.2f} dB (old) -> {rms(residual(result.x)):.2f} dB; per position "
          + ", ".join(f"{p} {e:.2f}" for p, e in zip(positions, per)))
    for k, n in ((2, "SHELF_LOW"), (3, "SHELF_HIGH")):
        print(f"    {n}_HZ = {shared[2 * k]:.1f}f, {n}_Q = {shared[2 * k + 1]:.3f}f")
    print("    SHELF_LOW_DB{" + ", ".join(f"{x:.2f}f" for x in low) + "}")
    print("    SHELF_HIGH_DB{" + ", ".join(f"{x:.2f}f" for x in high) + "}")
    print("    TONE_GAIN_DB{" + ", ".join(f"{x:.1f}f" for x in gain) + "}")


def ns2():
    d = load(BOSS / "ns-2" / "2026-10-27" / "fit" / "ns-2.json")["open_path"]
    f = np.array(d["hz"], dtype=float)
    target = np.array(d["db_re_bypass"])
    # A low shelf of +g (and -g of level) = a high shelf of -g: BiquadChain.h has no high
    # shelf. The open gain (its 1 kHz value) stays OPEN_GAIN_DB.
    open_db = target[list(f).index(1000)]
    fit_chain("NS-2 open path (re its 1 kHz gain; prints the low shelf, apply -gain as level)",
              Chain([["low_shelf", 7000.0, 0.7, 3.0]], [("hz", "q", "gain")]), f, target - open_db,
              SIX_PERCENT, 1000)


def main():
    dd8()
    bf3()
    cs3()
    ns2()


if __name__ == "__main__":
    main()
