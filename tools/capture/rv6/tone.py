#!/usr/bin/env python3
"""The RV-6's Tone, from the focused session: the filter on the wet (Rv6Reverb.h's TONE) and the low
reverb Tone adds below noon in Plate, Hall and Room (LOW).

The filter. Room, Hall, Plate and Spring are time-invariant, so a take's sweep response over the
noon take's is Tone's filter itself, read as a coherent ratio per sixth octave (what Tone adds
beside the filtered wet is another signal and averages out): the four modes agree within 0.1 dB at
3:00 and 5:00, and at 7:00 wherever the added reverb leaves them coherent (Spring has none: it
gives the bass). The 9:00 block was recorded at 12:00, so 9:00 is October's Hall (`tone_law`, an
energy ratio) with the added reverb's share taken out as 7:00 shows it. Three sections per
position, fitted to the band averages.

The low reverb. At Tone 7:00 the two outputs of Plate, Hall and Room carry one more signal, the
same on both: its energy per third octave is the real part of the outputs' cross-spectrum (their
own wets are unlike and average out), and over time, 63-400 Hz, the half sum's energy less the
half difference's. NAM-X makes it with a tank of its own through two sections. Fitted here: its
RT60 per mode at Time 7:00, 12:00 and 5:00 (the pedal's own decay there: a line through its level
per 100 ms; 9:00 and 3:00 follow the mode's Time law between them), the sections and a level per
mode (the tank's response rendered alone, render_rv6's `low.` overrides, against the pedal's
energies at Time 12:00), and its level at Time's ends. 9:00 of Tone is October's Hall.

  rv6/tone.py            the targets, the fits and their errors
  rv6/tone.py --write    the same, and Rv6Reverb.h's FITTED TONE block rewritten
  rv6/tone.py --check    NAM-X as built (build-rv6/render_rv6) against the pedal: Tone's curve per
                         mode and the low reverb's energies

Run from the repository root."""
import json, pathlib, re, sys
import numpy as np, scipy.optimize, scipy.signal
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import focused, takes

RATE = takes.RATE
SIZE = 1 << 19
FREQ = np.fft.rfftfreq(SIZE, 1 / RATE)
GRID = 50 * 2 ** (np.arange(0, 48) / 6)          # sixth octaves, 50 Hz - 11.4 kHz
THIRDS = np.array([50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000])
LTI = ["spring", "plate", "hall", "room"]
LOW_MODES = ["plate", "hall", "room"]
OCTOBER = pathlib.Path("captures/boss/rv-6/2026-10-31-followup/fit/rv-6-followup.json")
HEADER = pathlib.Path("src/dsp/Rv6Reverb.h")
KINDS = {"LOW_PASS": 1, "HIGH_PASS": 2, "PEAK": 3, "LOW_SHELF": 4, "HIGH_SHELF": 5, "LOW_PASS_1": 6, "HIGH_PASS_1": 7}
NAMES = {v: k for k, v in KINDS.items()}


def section_response(kind, hz, q, gain_db, f):
    """|H|^2 of one of BiquadChain.h's sections at the frequencies `f` (48 kHz)."""
    w = 2 * np.pi * hz / RATE
    c, alpha, a = np.cos(w), np.sin(w) / (2 * q), 10 ** (gain_db / 40)
    if kind == KINDS["PEAK"]:
        b, den = [1 + alpha * a, -2 * c, 1 - alpha * a], [1 + alpha / a, -2 * c, 1 - alpha / a]
    elif kind == KINDS["LOW_SHELF"]:
        r = 2 * np.sqrt(a) * alpha
        b = [a * ((a + 1) - (a - 1) * c + r), 2 * a * ((a - 1) - (a + 1) * c), a * ((a + 1) - (a - 1) * c - r)]
        den = [(a + 1) + (a - 1) * c + r, -2 * ((a - 1) + (a + 1) * c), (a + 1) + (a - 1) * c - r]
    elif kind == KINDS["HIGH_SHELF"]:
        r = 2 * np.sqrt(a) * alpha
        b = [a * ((a + 1) + (a - 1) * c + r), -2 * a * ((a - 1) + (a + 1) * c), a * ((a + 1) + (a - 1) * c - r)]
        den = [(a + 1) - (a - 1) * c + r, 2 * ((a - 1) - (a + 1) * c), (a + 1) - (a - 1) * c - r]
    elif kind == KINDS["LOW_PASS"]:
        b, den = [(1 - c) / 2, 1 - c, (1 - c) / 2], [1 + alpha, -2 * c, 1 - alpha]
    elif kind == KINDS["LOW_PASS_1"]:
        k = np.tan(w / 2)
        b, den = [k, k, 0], [1 + k, k - 1, 0]
    else:
        raise ValueError(kind)
    z = np.exp(-2j * np.pi * np.asarray(f) / RATE)
    return np.abs((b[0] + b[1] * z + b[2] * z * z) / (den[0] + den[1] * z + den[2] * z * z)) ** 2


def chain_response(sections, f):
    out = np.ones(len(f))
    for kind, hz, q, gain_db in sections:
        out = out * section_response(kind, hz, q, gain_db, f)
    return out


FINE = 8                                           # points per band for the band averages


def band_average_db(sections, centres, fraction):
    """The chain's level per band (1/`fraction` octave wide), dB: the mean of |H|^2 over each band."""
    offsets = (np.arange(FINE) + 0.5) / FINE - 0.5
    f = (np.asarray(centres)[:, None] * 2 ** (offsets[None, :] / fraction)).ravel()
    return 10 * np.log10(chain_response(sections, f).reshape(len(centres), FINE).mean(axis=1))


# --- the filter's targets ---

def responses(name):
    """A sweep take's wet responses, outputs A and B (the pedal)."""
    meta, played, *_ = takes.load(name)
    part, start, body, tail = focused.parts_of(meta)[0]
    return [focused.response(w, played, start, body, tail)[0] for w in takes.wet(name)]


def coherent_ratio(num, den):
    """Per sixth octave: |sum(H_num conj(H_den))| / sum(|H_den|^2), dB, and the coherence, the mean of
    both outputs."""
    mags, cohs = [], []
    for hn, hd in zip(responses(num), responses(den)):
        n = min(len(hn), len(hd), 2 * RATE)
        a, b = np.fft.rfft(hn[:n], SIZE), np.fft.rfft(hd[:n], SIZE)
        mag, coh = [], []
        for c in GRID:
            m = (FREQ >= c / 2 ** (1 / 12)) & (FREQ < c * 2 ** (1 / 12))
            cross = np.sum(a[m] * np.conj(b[m]))
            mag.append(np.abs(cross) / np.sum(np.abs(b[m]) ** 2))
            coh.append(np.abs(cross) ** 2 / (np.sum(np.abs(a[m]) ** 2) * np.sum(np.abs(b[m]) ** 2)))
        mags.append(mag)
        cohs.append(coh)
    return 20 * np.log10(np.mean(mags, axis=0)), np.mean(cohs, axis=0)


def filter_targets():
    """{position: Tone's filter re noon per sixth octave (GRID), dB} for 7:00, 9:00, 3:00, 5:00."""
    out = {}
    for position in ("0700", "0300", "0500"):
        rows = []
        for mode in LTI:
            mag, coh = coherent_ratio(f"tone-{position}-{mode}", f"noon-{mode}")
            # At 7:00 under 700 Hz only Spring is the filter alone (the others carry the low
            # reverb); in the notch the wet is 20 dB down and less coherent.
            ok = (coh > 0.6) & ((GRID >= 700) | (mode == "spring") | (position != "0700"))
            rows.append(np.where(ok, mag, np.nan))
        out[position] = np.nanmedian(rows, axis=0)
    # 9:00: October's Hall, an energy ratio; the low reverb's share (7:00's energy ratio less 7:00's
    # filter, 1.5 dB lower at 9:00) taken out, and nothing under 700 Hz, as at 7:00.
    law = json.loads(OCTOBER.read_text())["tone_law"]
    hz = np.array(law["centres_hz"])
    energy = {k: np.interp(np.log(GRID), np.log(hz), np.array(v)) for k, v in law["re_noon_db"].items()}
    added = np.maximum(10 ** (energy["7:00"] / 10) - 10 ** (out["0700"] / 10), 0)
    nine = 10 * np.log10(np.maximum(10 ** (energy["9:00"] / 10) - 10 ** -0.15 * added, 1e-3))
    out["0900"] = np.where(GRID < 700, 0.0, nine)
    out["0900"][GRID > hz[-1]] = np.nan
    return out


# Per position: the sections' kinds and where the fit starts (hz, q, dB).
STARTS = {
    "0700": [("HIGH_SHELF", 1700, 0.6, -7.5), ("PEAK", 4030, 5.0, -18.0), ("PEAK", 3200, 1.2, -4.0)],
    "0900": [("HIGH_SHELF", 2600, 0.7, -5.0), ("PEAK", 4030, 5.0, -8.0), ("PEAK", 3500, 1.5, -2.0)],
    "0300": [("LOW_SHELF", 90, 0.8, -6.0), ("PEAK", 1200, 2.5, 2.0), ("HIGH_SHELF", 4800, 0.7, 8.3)],
    "0500": [("LOW_SHELF", 90, 0.8, -6.0), ("PEAK", 1200, 2.5, 0.5), ("HIGH_SHELF", 1900, 0.6, 9.9)],
}


def fit_filter(target, start):
    keep = ~np.isnan(target)
    kinds = [KINDS[s[0]] for s in start]

    def unpack(p):
        return [(kinds[i], float(np.exp(p[3 * i])), float(np.exp(p[3 * i + 1])), float(p[3 * i + 2])) for i in range(len(kinds))]

    def error(p):
        return (band_average_db(unpack(p), GRID, 6) - target)[keep]

    p0 = np.concatenate([[np.log(hz), np.log(q), gain] for _, hz, q, gain in start])
    lower = np.concatenate([[np.log(40), np.log(0.3), -40] for _ in start])
    upper = np.concatenate([[np.log(12000), np.log(12), 20] for _ in start])
    fit = scipy.optimize.least_squares(error, p0, bounds=(lower, upper))
    return unpack(fit.x), float(np.sqrt(np.mean(fit.fun ** 2))), float(np.max(np.abs(fit.fun)))


# --- the low reverb ---

def mono_energy(pair, seconds=2.0):
    """Per third octave: what the two outputs share (the cross-spectrum's real part) and the outputs'
    mean energy."""
    n = int(seconds * RATE)
    a, b = np.fft.rfft(pair[0][:n], SIZE), np.fft.rfft(pair[1][:n], SIZE)
    cross, total = np.real(a * np.conj(b)), 0.5 * (np.abs(a) ** 2 + np.abs(b) ** 2)
    bands = [(FREQ >= c / 2 ** (1 / 6)) & (FREQ < c * 2 ** (1 / 6)) for c in THIRDS]
    return np.array([cross[m].sum() for m in bands]), np.array([total[m].sum() for m in bands])


def model_responses(name, overrides):
    meta, played, *_ = takes.load(name)
    part, start, body, tail = focused.parts_of(meta)[0]
    return [focused.response(w, played, start, body, tail)[0] for w in takes.model_wet(name, overrides)]


FLAT = "low.time_db=0,0,0,0,0"
LOW_BAND = (THIRDS >= 63) & (THIRDS <= 400)
LOW_SPAN = scipy.signal.butter(4, [63, 400], "bandpass", fs=RATE, output="sos")
LOW_STEP = RATE // 10
TIMES = {"0700": 0, "1200": 2, "0500": 4}


def low_take(mode, position):
    return f"tone-0700-{mode}" if position == "1200" else f"tone-0700-time-{position}-{mode}"


def low_alone(name, overrides):
    """NAM-X's low reverb alone in the take `name` (the same on both outputs): the response with
    it less the response without."""
    with_it = model_responses(name, list(overrides) + ["low.at=1,1,1,1,1"])
    without = model_responses(name, ["low.gain_db=0", "low.at=0,0,0,0,0"])
    return with_it[0] - without[0]


def third_energy(h, seconds=2.0):
    spectrum = np.abs(np.fft.rfft(h[:int(seconds * RATE)], SIZE)) ** 2
    return np.array([spectrum[(FREQ >= c / 2 ** (1 / 6)) & (FREQ < c * 2 ** (1 / 6))].sum() for c in THIRDS])


def decay_time(level_db):
    """RT60 of a level per 100 ms (dB): a line from its top down 30 dB."""
    top = int(np.argmax(level_db))
    under = np.nonzero(level_db[top:] < level_db[top] - 30)[0]
    end = top + (under[0] if len(under) else len(level_db) - top)
    if end - top < 3:
        return float("nan")
    slope = np.polyfit(np.arange(top, end) * LOW_STEP / RATE, level_db[top:end], 1)[0]
    return float(-60 / slope)


def steps(x):
    n = len(x) // LOW_STEP
    return np.mean(x[:n * LOW_STEP].reshape(n, LOW_STEP) ** 2, axis=1)


def twin(name):
    """The take with the same Time and Tone at 12:00."""
    return name.replace("tone-0700-time", "time") if "time" in name else name.replace("tone-0700", "noon")


def pedal_low(name):
    """The pedal's low reverb alone in a Tone 7:00 take, good under 500 Hz: the response less the
    Tone 12:00 twin's (Tone's filter is flat there, and these modes repeat to the sample: the tank's
    own wet cancels 30 dB or more), the two outputs' mean."""
    seven, noon = responses(name), responses(twin(name))
    n = min(len(seven[0]), len(noon[0]))
    return 0.5 * ((seven[0][:n] - noon[0][:n]) + (seven[1][:n] - noon[1][:n]))


def pedal_low_decay(name):
    """The pedal's low reverb's RT60 in the take, 63-400 Hz."""
    return decay_time(10 * np.log10(steps(scipy.signal.sosfiltfilt(LOW_SPAN, pedal_low(name))) + 1e-30))


def model_low_decay(name, overrides):
    return decay_time(10 * np.log10(steps(scipy.signal.sosfiltfilt(LOW_SPAN, low_alone(name, overrides))) + 1e-30))


def time_law(mode):
    """The mode's RT60 at 1 kHz at the five Time positions, from the header."""
    import fit_rv6
    _, _, _, voicings = fit_rv6.read_block()
    return voicings[[m.lower() for m in takes.MODES].index(mode)]["rt"]


def low_decays():
    """{mode: the low reverb's RT60 at the five Time positions, s}, and the pedal's readings at
    7:00, 12:00 and 5:00: the tank's RT60 there is what makes NAM-X's low reverb read as the pedal's
    (one correction from the reading itself: a short tail reads longer than the tank is set);
    9:00 and 3:00 placed between them as the mode's own Time law places its."""
    out, read = {}, {}
    for mode in LOW_MODES:
        read[mode] = [pedal_low_decay(low_take(mode, position)) for position in TIMES]
        guess = [read[mode][0], read[mode][0], read[mode][1], read[mode][2], read[mode][2]]
        ours = [model_low_decay(low_take(mode, position), ["low.gain_db=0", "low.band=0,0,0,0", FLAT, "low.rt60=" + table(guess)])
                for position in TIMES]
        seven, noon, five = (p * p / m for p, m in zip(read[mode], ours))
        law = np.log(time_law(mode))
        nine = seven * (noon / seven) ** ((law[1] - law[0]) / (law[2] - law[0]))
        three = noon * (five / noon) ** ((law[3] - law[2]) / (law[4] - law[2]))
        out[mode] = [seven, float(nine), noon, float(three), five]
    return out, read


def table(values):
    return ",".join(f"{v:.6g}" for v in values)


LOW_START = [("LOW_SHELF", 150, 0.5, 9.0), ("LOW_PASS_1", 450, 0.7071, 0.0)]


def band_at_unity(sections):
    """The chain as BiquadChain::design scales it: unity at 1 kHz."""
    return 10 * np.log10(chain_response(sections, np.array([1000.0])))[0]


def fit_low(decays):
    """The low reverb's two sections and a level per mode against the pedal's shared energy at Tone
    7:00, 50 Hz-5 kHz (the bands over 500 Hz, where it is a small part of the wet, count a third)."""
    # Under 500 Hz the take less its twin; above, what the outputs share (Tone's filter differs there).
    pedal = {mode: np.where(THIRDS <= 400, third_energy(pedal_low(f"tone-0700-{mode}")),
                            mono_energy(responses(f"tone-0700-{mode}"))[0]) for mode in LOW_MODES}
    raw = {mode: third_energy(low_alone(f"tone-0700-{mode}", ["low.gain_db=0", "low.band=0,0,0,0", FLAT,
                                                             "low.rt60=" + table(decays[mode])])) for mode in LOW_MODES}
    weight = np.where(THIRDS <= 500, 1.0, 0.33)
    kinds = [KINDS[s[0]] for s in LOW_START]

    def unpack(p):
        shelf = (kinds[0], float(np.exp(p[0])), float(np.exp(p[1])), float(p[2]))
        return [shelf, (kinds[1], float(np.exp(p[3])), 0.7071, 0.0)], p[4:]

    def model_db(p):
        sections, gains = unpack(p)
        shape = band_average_db(sections, THIRDS, 3) - band_at_unity(sections)
        return {mode: 10 * np.log10(raw[mode]) + shape + gains[i] for i, mode in enumerate(LOW_MODES)}

    def error(p):
        model = model_db(p)
        rows = []
        for mode in LOW_MODES:
            ok = pedal[mode] > 0
            rows.append(((model[mode] - 10 * np.log10(np.where(ok, pedal[mode], 1))) * weight)[ok])
        return np.concatenate(rows)

    p0 = np.array([np.log(LOW_START[0][1]), np.log(LOW_START[0][2]), LOW_START[0][3], np.log(LOW_START[1][1]), -20, -20, -20])
    lower = [np.log(40), np.log(0.3), 0, np.log(100), -80, -80, -80]
    upper = [np.log(600), np.log(1.2), 30, np.log(3000), 20, 20, 20]
    fit = scipy.optimize.least_squares(error, p0, bounds=(lower, upper))
    sections, gains = unpack(fit.x)
    return sections, {mode: float(g) for mode, g in zip(LOW_MODES, gains)}, pedal, model_db(fit.x)


def band_text(sections):
    return "low.band=" + ",".join(f"{kind},{hz:.6g},{q:.6g},{gain_db:.6g}" for kind, hz, q, gain_db in sections)


def fit_low_time(sections, gains, decays):
    """{mode: the low reverb's level at the five Time positions re 12:00, dB}: what brings the fitted
    reverb's energy, 63-400 Hz, to the pedal's at Time 7:00 and 5:00 (Tone 7:00), over what a longer
    tail's quieter start already does. 9:00 takes 7:00's, 3:00 the middle of 12:00 and 5:00."""
    out = {}
    for mode in LOW_MODES:
        fixed = [f"low.gain_db={gains[mode]:.6g}", band_text(sections), "low.rt60=" + table(decays[mode]), FLAT]
        level = {}
        for position in ("0700", "0500"):
            name = low_take(mode, position)
            pedal = third_energy(pedal_low(name))[LOW_BAND].sum()
            level[position] = float(10 * np.log10(pedal / third_energy(low_alone(name, fixed))[LOW_BAND].sum()))
        out[mode] = [level["0700"], level["0700"], 0.0, 0.5 * level["0500"], level["0500"]]
    return out


def low_at_nine():
    """The low reverb at Tone 9:00 re 7:00, dB. October's Hall at E.Level 12:00: the energy ratios at
    100-200 Hz less the wet's own (0 dB there) at both positions."""
    law = json.loads(OCTOBER.read_text())["tone_law"]
    hz = np.array(law["centres_hz"])
    band = (hz >= 95) & (hz <= 210)
    added = {k: np.mean(10 ** (np.array(law["re_noon_db"][k])[band] / 10) - 1) for k in ("7:00", "9:00")}
    return float(10 * np.log10(added["9:00"] / added["7:00"]))


# --- the header ---

def number(value, digits=4):
    """A float literal: `digits` significant digits, always with a decimal point."""
    text = f"{value:.{digits}g}"
    return (text if "." in text or "e" in text else text + ".0") + "f"


def section_text(section):
    kind, hz, q, gain_db = section
    return f"{{FilterKind::{NAMES[kind]}, {number(hz, 5)}, {number(q)}, {number(gain_db)}}}"


def block_text(filters, low_sections, low_gains, nine_db, decays, low_time):
    rows = []
    for position in ("0700", "0900", None, "0300", "0500"):
        rows.append("    {{}}," if position is None else "    {{" + ",\n      ".join(section_text(s) for s in filters[position]) + "}},")
    gains = ["NO_LOW", "NO_LOW"] + [number(low_gains[m]) for m in LOW_MODES] + ["NO_LOW"]
    return ("// BEGIN FITTED TONE (tools/capture/rv6/tone.py --write rewrites this block)\n"
            "constexpr ToneShape TONE{{{\n" + "\n".join(rows) + "\n}}};\n\n"
            "constexpr LowShape LOW{{{" + ",\n                         ".join(section_text(s) for s in low_sections) + "}},\n"
            "                       {" + ", ".join(gains) + "},\n"
            f"                       {{1.0f, {number(10 ** (nine_db / 20))}, 0.0f, 0.0f, 0.0f}},\n"
            + "".join("                       {{{},\n                         {},\n" +
                      "".join("                         {" + ", ".join(number(v, 3) for v in rows[m]) + "},\n" for m in LOW_MODES) +
                      "                         {}}}" + end for rows, end in ((decays, ",\n"), (low_time, "};\n")))
            + "// END FITTED TONE\n")


def cells(values, form="{:6.1f}"):
    return " ".join("     -" if v is None or np.isnan(v) else form.format(v) for v in values)


def fit_all(write):
    targets = filter_targets()
    filters = {}
    print("Tone's filter re noon, dB per sixth octave: the pedal, the three sections, their difference")
    print("hz        " + cells(GRID, "{:6.0f}"))
    for position in ("0700", "0900", "0300", "0500"):
        filters[position], rms, worst = fit_filter(targets[position], STARTS[position])
        model = band_average_db(filters[position], GRID, 6)
        print(f"{position} pedal " + cells(targets[position]))
        print(f"     model " + cells(model))
        print(f"     diff  " + cells(model - targets[position]) + f"   rms {rms:.2f} dB, worst {worst:.2f}")
        for s in filters[position]:
            print("     " + section_text(s))
    decays, pedal_decays = low_decays()
    low_sections, low_gains, pedal, model = fit_low(decays)
    nine_db = low_at_nine()
    print("\nThe low reverb at Tone 7:00: what the outputs share, energy per third octave, dB (pedal, NAM-X's tap)")
    print("hz           " + cells(THIRDS, "{:6.0f}"))
    for mode in LOW_MODES:
        print(f"{mode:6s} pedal " + cells([10 * np.log10(v) if v > 0 else np.nan for v in pedal[mode]]))
        print(f"       model " + cells(model[mode]) + f"   gain {low_gains[mode]:+.2f} dB")
    for s in low_sections:
        print("     " + section_text(s))
    print(f"at 9:00 re 7:00: {nine_db:+.2f} dB (October's Hall)")
    low_time = fit_low_time(low_sections, low_gains, decays)
    for mode in LOW_MODES:
        fixed = [f"low.gain_db={low_gains[mode]:.6g}", band_text(low_sections), "low.rt60=" + table(decays[mode]),
                 "low.time_db=" + table(low_time[mode])]
        ours = [model_low_decay(low_take(mode, position), fixed) for position in TIMES]
        print(f"{mode:6s} the tank's RT60 at Time 7:00, 9:00, 12:00, 3:00, 5:00, s: " + cells(decays[mode], "{:6.2f}")
              + "   (read at 7:00, 12:00, 5:00: the pedal " + cells(pedal_decays[mode], "{:.2f}") + ", NAM-X " + cells(ours, "{:.2f}") + ")")
        print(f"{'':6s} its level there re 12:00, dB:                 " + cells(low_time[mode]))
    if write:
        text = HEADER.read_text()
        text = re.sub(r"// BEGIN FITTED TONE.*?// END FITTED TONE\n", lambda _: block_text(filters, low_sections, low_gains, nine_db, decays, low_time), text, flags=re.S)
        HEADER.write_text(text)
        print(f"wrote {HEADER}: rebuild build-rv6/render_rv6")


def check():
    """NAM-X as built against the pedal, per mode: Tone's curve (third octaves of the sweep's
    response re the noon take's, both outputs' mean) and the outputs' shared energy at 7:00."""
    thirds = THIRDS
    def curve(pairs_num, pairs_den):
        out = []
        for hn, hd in zip(pairs_num, pairs_den):
            n = min(len(hn), len(hd), 2 * RATE)
            a, b = np.abs(np.fft.rfft(hn[:n], SIZE)) ** 2, np.abs(np.fft.rfft(hd[:n], SIZE)) ** 2
            out.append([10 * np.log10(a[m].sum() / b[m].sum()) for m in [(FREQ >= c / 2 ** (1 / 6)) & (FREQ < c * 2 ** (1 / 6)) for c in thirds]])
        return np.mean(out, axis=0)
    print("Tone re noon, third octaves, dB: pedal / NAM-X / difference")
    print("hz" + " " * 31 + cells(thirds, "{:6.0f}"))
    for mode in ["modulate"] + LTI:
        noon_p, noon_m = responses(f"noon-{mode}"), model_responses(f"noon-{mode}", [])
        for take in (f"tone-0700-{mode}", f"tone-0700-time-0700-{mode}", f"tone-0700-time-0500-{mode}", f"tone-0300-{mode}", f"tone-0500-{mode}"):
            base = take.replace("tone-0700-", "").replace("tone-0500-", "") if "time" in take else None
            den_p = responses(base) if base else noon_p
            den_m = model_responses(base, []) if base else noon_m
            p, m = curve(responses(take), den_p), curve(model_responses(take, []), den_m)
            print(f"{take:28s} p " + cells(p))
            print(f"{'':28s} m " + cells(m))
            print(f"{'':28s} d " + cells(m - p) + f"   rms {np.sqrt(np.mean((m - p) ** 2)):.2f}")
    print("\nShared by the two outputs at Tone 7:00, energy per third octave, dB: pedal / NAM-X")
    for mode in LOW_MODES:
        for take in (f"tone-0700-{mode}", f"tone-0700-time-0700-{mode}", f"tone-0700-time-0500-{mode}"):
            p = mono_energy(responses(take))[0]
            m = mono_energy(model_responses(take, []))[0]
            print(f"{take:28s} p " + cells([10 * np.log10(v) if v > 0 else np.nan for v in p]))
            print(f"{'':28s} m " + cells([10 * np.log10(v) if v > 0 else np.nan for v in m]))


if __name__ == "__main__":
    if "--check" in sys.argv:
        check()
    else:
        fit_all("--write" in sys.argv)
