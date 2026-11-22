"""NS-2 control laws scored on every recording at once (dataset.py): the older takes, the 10-30 history
probes and the 11-08 reamp — so a law that fits one set can't quietly break another. Laws run at a 1 ms
control rate on the detector's level (NAM-X runs per sample; check_engine compares the two).

  refit.py score [LAW]        the law's error per set and per take (default: baseline, NAM-X's model)
                              candidate = peak + rectified mean, continuous closing control
  refit.py check              the baseline law here against NAM-X's own processor (render_ns2) on the reamp
  refit.py sweep              the 2026-11-08 search: NAM-X's law and the extended law (refitted, with per-knob
                              threshold corrections) with no detector change, a 150 Hz bass cut, a 1 ms attack
Needs build-ns2/reamp-dataset.npz (dataset.py) and the detector variants (weighting.py).
Run from the repository root."""
import pathlib, re, subprocess, sys
import numpy as np, numba

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import dataset as D

HEADER = (D.ROOT / "src/dsp/Ns2Gate.h").read_text()


def header_table(name):
    body = re.search(name + r"\{([^}]*)\}", HEADER).group(1)
    return np.array([float(v.rstrip("f")) for v in body.split(",")])


TAGS = [D.tag(c) for c in D.CLOCKS]
THRESHOLD_DB = dict(zip(TAGS, header_table("THRESHOLD_DB")))
DECAY_S = dict(zip(TAGS, header_table("DECAY_MS") / 1000))
DT = 0.001
REAMP_GAIN0 = 0.45     # the open path re bypass on the DI dataset (+0.55 dB at 80 Hz … +0.31 at 2 kHz)


# ---- laws: (levels, threshold dB, decay s, gain0, params) → gain dB per ms -----------------------------

@numba.njit(cache=True)
def baseline(levels, threshold, decay, gain0, p):
    """NAM-X's model (Ns2Gate.h). p: jump, open, hysteresis, close, slope, ratio."""
    jump, open_db, hyst, close, slope, ratio = p[0], p[1], p[2], p[3], p[4], p[5]
    fast = slow = -140.0
    switch = -jump
    opened = False
    out = np.empty(len(levels))
    af = -np.expm1(-DT / decay)
    av = -np.expm1(-DT / (decay * ratio))
    ag = -np.expm1(-DT / (decay * close))
    for i in range(len(levels)):
        level = levels[i]
        fast = level if level > fast else fast + (level - fast) * af
        slow = level if level > slow else slow + (level - slow) * av
        if fast > threshold + open_db:
            opened = True
        elif fast < threshold + open_db - hyst:
            opened = False
        switch = 0.0 if opened else switch + (-jump - switch) * ag
        out[i] = gain0 + max(-140.0, switch - slope * max(0.0, threshold - slow))
    return out


BASELINE_P = np.array([7.56, 1.29, 10.47, 4.42, 0.889, 7.94])


@numba.njit(cache=True)
def candidate(levels, threshold, decay, gain0, p):
    """levels[:, 0] = peak dB, levels[:, 1] = peak/mean ratio dB.

    Strong peaks open at once. The closing control accumulates only while the
    peak/mean-dependent closing condition holds; otherwise it retains its value.
    This removes the binary latch, so a brief crossing cannot start an entire
    closing trajectory. The deeper-tail envelope keeps the shipping law.
    """
    weight, knee = p
    fast = slow = -140.0
    control = -7.56
    out = np.empty(len(levels))
    af = -np.expm1(-DT / decay)
    av = -np.expm1(-DT / (decay * 7.94))
    ag = -np.expm1(-DT / (decay * 4.42))
    for i in range(len(levels)):
        level = levels[i, 0]
        crest = max(0.0, levels[i, 1] - knee)
        fast = max(level, fast + (level - fast) * af)
        slow = max(level, slow + (level - slow) * av)
        if fast > threshold + 1.29:
            control = 0.0
        elif fast - weight * crest < threshold + 1.29 - 10.47:
            control += (-7.56 - control) * ag
        out[i] = gain0 + max(-140.0, control - 0.889 * max(0.0, threshold - slow))
    return out


CANDIDATE_P = np.array([2.0, 6.0])
LAWS = {"baseline": (baseline, BASELINE_P), "candidate": (candidate, CANDIDATE_P)}


# ---- scoring -----------------------------------------------------------------------------------------

def knob_maps(p_map=None):
    """Threshold/Decay per knob position; p_map (optional) offsets the five thresholds."""
    th = dict(THRESHOLD_DB)
    if p_map is not None:
        for k, off in zip(TAGS, p_map):
            th[k] += off
    return th, DECAY_S


def predict(take, law, p, th):
    gain0 = take.get("gain0", REAMP_GAIN0)
    levels = take["levels"]
    if law is candidate:
        from rectified import ratios
        levels = np.column_stack((levels, ratios(sets())[take["name"]]))
    return law(levels, th[take["threshold"]], DECAY_S[take["decay"]], gain0, p)


WINDOW_MS = 20          # a 20 ms window of hiss 12 dB over the floor reads within ~1 dB; 10 ms at 6 dB didn't
OVER_FLOOR_DB = 12.0
JUMP_DB = 6.0           # windows next to a level jump this big are left out (a 1-window timing slip there is
                        # tens of dB; the older takes' mask does the same)


def reamp_windows(take, gain_db):
    """WINDOW_MS windows from the signal's start: the prediction (floor added) and the pedal, in dB; and which
    windows are scored (the open path OVER_FLOOR_DB over the floor, no level jump next to them)."""
    a = take["start"]
    w = WINDOW_MS
    n = (len(take["open"]) - a) // w
    sl = slice(a, a + w * n)
    pred = (take["open"][sl] * 10 ** (gain_db[sl] / 10)).reshape(n, w).mean(axis=1) + take["floor"]
    pedal = take["pedal"][sl].reshape(n, w).mean(axis=1)
    open_w = take["open"][sl].reshape(n, w).mean(axis=1)
    open_db = 10 * np.log10(open_w + 1e-30)
    jump = np.abs(np.diff(open_db, prepend=open_db[0])) > JUMP_DB
    jump = jump | np.r_[jump[1:], False] | np.r_[False, jump[:-1]]
    # A window where the pedal is louder than the open path holds one of its switching clicks (a DC step's
    # edge, up to ~30 dB over its input), not a gain.
    click = pedal > open_w * 10 ** (3 / 10)
    seen = (open_w > take["floor"] * 10 ** (OVER_FLOOR_DB / 10)) & ~jump & ~click
    level = take["levels"][sl].reshape(n, w).max(axis=1)
    return 10 * np.log10(pred), 10 * np.log10(pedal + 1e-30), seen, level


def errors(take, gain_db):
    """The residual (model − pedal, dB) of a take, over the samples or windows it is scored on."""
    if "open" in take:
        pred, pedal, seen, _ = reamp_windows(take, gain_db)
        return (pred - pedal)[seen]
    return (gain_db - take["target"])[take["valid"]]


def rms(x):
    return float(np.sqrt(np.mean(x ** 2))) if len(x) else float("nan")


SETS = {}


def sets():
    if not SETS:
        SETS["older"] = D.older()
        SETS["history"] = D.history()
        SETS["reamp"] = D.reamp()
    return SETS


def history_groups(take, gain_db):
    return [rms((gain_db - take["target"])[g["indices"]]) for g in take["groups"]]


def score(law, p, p_map=None, verbose=False):
    th, _ = knob_maps(p_map)
    out = {}
    for set_name, takes in sets().items():
        per = []
        for take in takes:
            g = predict(take, law, p, th)
            if set_name == "history":
                per += [(f"{take['name']} {grp['level']} {grp['history']}", e)
                        for grp, e in zip(take["groups"], history_groups(take, g))]
            else:
                per.append((take["name"], rms(errors(take, g))))
        out[set_name] = per
    if verbose:
        for set_name, per in out.items():
            v = np.array([e for _, e in per])
            print(f"== {set_name}: median {np.median(v):.2f} dB, mean {v.mean():.2f}, worst {v.max():.2f} "
                  f"({per[int(v.argmax())][0]})")
            for name, e in per:
                print(f"   {e:6.2f}  {name}")
    return out


def check():
    """The 1 ms baseline against NAM-X's processor (per sample) on the reamp: the residual's rms each."""
    import soundfile
    th, _ = knob_maps()
    for take in sets()["reamp"]:
        name = take["name"].split("/", 1)[1]
        played, _ = soundfile.read(D.REAMP / "played" / f"{name}.wav", dtype="float32")
        ref = 10 ** (-4.67 / 20)
        opens = th[take["threshold"]] + BASELINE_P[1] - 6.0
        y = np.frombuffer(subprocess.run(["build-ns2/render_ns2", f"{opens}", f"{DECAY_S[take['decay']] * 1000}"],
                                         input=(played * (10 ** (-6 / 20) / ref)).tobytes(), capture_output=True,
                                         check=True).stdout, np.float32).astype(float) * (ref / 10 ** (-6 / 20))
        x = played.astype(float)
        n = len(take["levels"])
        engine = 10 * np.log10(D.per_ms(y ** 2)[:n] / np.maximum(D.per_ms(x ** 2)[:n], 1e-30) + 1e-30)
        here = baseline(take["levels"], th[take["threshold"]], DECAY_S[take["decay"]], 0.6, BASELINE_P)[:n]
        loud = D.per_ms(x ** 2)[:n] > ref ** 2 * 10 ** (-80 / 10)
        e_here, e_engine = rms(errors(take, here)), rms(errors(take, engine - 0.15))
        print(f"{name:34s} engine vs 1 ms law, loud ms: median |Δ| {np.median(np.abs(engine - here)[loud]):.2f} dB; "
              f"reamp rms error: law {e_here:.2f}, engine {e_engine:.2f}")


def sweep():
    for label, kw in [("no detector change", {}), ("detector HP1 150 Hz", {"corner": 150}),
                      ("detector attack 1 ms", {"attack": 1.0})]:
        s = Setup(maps=("switch", "slope"), **kw)
        s.report(s.start(np.r_[EXT_P0[:7], 140.0]), f"NAM-X's law, {label}")
        q = s.fit(s.start(), label=f"extended refit, {label}")
        print("   ", dict(zip(EXT_NAMES, np.round(q[:8], 3))), "switch Δ", np.round(q[8:13], 1), "slope Δ",
              np.round(q[13:18], 1), flush=True)


# ---- fitting -----------------------------------------------------------------------------------------

SET_WEIGHT = {"older": 1.0, "history": 1.0, "reamp": 1.0}


def residuals(law, p, th, takes_by_set, weights=SET_WEIGHT):
    """Every take's residual, scaled so each take weighs alike within its set and each set as its weight."""
    rows = []
    for set_name, takes in takes_by_set.items():
        w = np.sqrt(weights[set_name] / len(takes))
        for take in takes:
            g = predict(take, law, p, th)
            if set_name == "history":
                for grp in take["groups"]:
                    e = (g - take["target"])[grp["indices"]]
                    rows.append(e * w / np.sqrt(len(e) * len(take["groups"]) / 1.0))
            else:
                e = errors(take, g)
                if len(e):
                    rows.append(e * w / np.sqrt(len(e)))
    return np.concatenate(rows)


def summary(law, p, th, takes_by_set, label=""):
    out = []
    for set_name, takes in takes_by_set.items():
        v = []
        for take in takes:
            g = predict(take, law, p, th)
            v += history_groups(take, g) if set_name == "history" else [rms(errors(take, g))]
        v = np.array(v)
        out.append(f"{set_name} med {np.median(v):.2f} mean {v.mean():.2f} max {v.max():.2f}")
    print(f"{label:28s} " + " | ".join(out), flush=True)


def fit(law, p0, lower, upper, takes_by_set, th=None, weights=SET_WEIGHT, label="fit"):
    from scipy import optimize
    th = th or knob_maps()[0]
    r = optimize.least_squares(lambda p: residuals(law, p, th, takes_by_set, weights), p0, bounds=(lower, upper),
                               x_scale="jac", diff_step=1e-3, max_nfev=400)
    summary(law, r.x, th, takes_by_set, label)
    return r.x


# ---- the extended law: the baseline plus what the 5:00 data asks for -----------------------------------

@numba.njit(cache=True)
def extended(levels, t_switch, t_slope, decay, gain0, p):
    """The baseline with: the slope term's share while the switch is open (p[6]: 1 = the baseline, 0 = fully
    open), a ceiling on the reduction (p[7], smooth), and separate thresholds for the switch and the slope
    term (t_switch, t_slope). p: jump, open, hysteresis, close, slope, ratio, open share, ceiling."""
    jump, open_db, hyst, close, slope, ratio, share, ceiling = p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]
    fast = slow = -140.0
    switch = -jump
    opened = False
    out = np.empty(len(levels))
    af = -np.expm1(-DT / decay)
    av = -np.expm1(-DT / (decay * ratio))
    ag = -np.expm1(-DT / (decay * close))
    for i in range(len(levels)):
        level = levels[i]
        fast = level if level > fast else fast + (level - fast) * af
        slow = level if level > slow else slow + (level - slow) * av
        if fast > t_switch + open_db:
            opened = True
        elif fast < t_switch + open_db - hyst:
            opened = False
        switch = 0.0 if opened else switch + (-jump - switch) * ag
        expand = slope * max(0.0, t_slope - slow) * (share if opened else 1.0)
        r = expand - switch
        r = r / (1.0 + (r / ceiling) ** 4) ** 0.25
        out[i] = gain0 - min(140.0, r)
    return out


EXT_NAMES = ["jump", "open", "hyst", "close", "slope", "ratio", "share", "ceiling"]
EXT_P0 = np.array([7.56, 1.29, 10.47, 4.42, 0.889, 7.94, 1.0, 60.0])
EXT_LO = np.array([1.0, -6.0, 2.0, 0.3, 0.2, 1.0, 0.0, 15.0])
EXT_HI = np.array([25.0, 8.0, 25.0, 25.0, 4.0, 40.0, 1.0, 140.0])


class Setup:
    """A law with its knob-map corrections and a detector weighting, over the three sets."""

    def __init__(self, order=1, corner=0, maps=("switch", "slope"), attack=0.0):
        import weighting as W
        self.maps = maps
        base = sets()
        if attack > 0:
            reamp_lv = np.load(W.attack_path(attack), allow_pickle=True).item()
            cal = W.tone_offset_db(attack, 1000)
            tone = {f: W.tone_offset_db(attack, f) - cal for f in (220, 1000)}
        else:
            reamp_lv = W.reamp_levels(order, corner)
            tone = {f: W.offset_db(order, corner, f) for f in (220, 1000)}
        self.takes = {
            "older": [dict(t, levels=t["levels"] + tone[t["freq"]]) for t in base["older"]],
            "history": [dict(t, levels=t["levels"] + tone[1000]) for t in base["history"]],
            "reamp": [dict(t, levels=reamp_lv[t["name"].split("/", 1)[1]]) for t in base["reamp"]],
        }
        self.n_law = len(EXT_P0)

    def split(self, q):
        p = q[:self.n_law]
        offsets = {}
        k = self.n_law
        for m in ("switch", "slope"):
            if m in self.maps:
                offsets[m] = dict(zip(TAGS, q[k:k + 5]))
                k += 5
            else:
                offsets[m] = dict.fromkeys(TAGS, 0.0)
        if "tied" in self.maps:
            offsets["slope"] = offsets["switch"]
        return p, offsets

    def gain(self, take, q):
        p, off = self.split(q)
        th = THRESHOLD_DB[take["threshold"]]
        return extended(take["levels"], th + off["switch"][take["threshold"]], th + off["slope"][take["threshold"]],
                        DECAY_S[take["decay"]], take.get("gain0", REAMP_GAIN0), p)

    def residuals(self, q, weights=SET_WEIGHT):
        rows = []
        for set_name, takes in self.takes.items():
            w = np.sqrt(weights[set_name] / len(takes))
            for take in takes:
                g = self.gain(take, q)
                if set_name == "history":
                    for grp in take["groups"]:
                        e = (g - take["target"])[grp["indices"]]
                        rows.append(e * w / np.sqrt(len(e) * len(take["groups"])))
                else:
                    e = errors(take, g)
                    if len(e):
                        rows.append(e * w / np.sqrt(len(e)))
        return np.concatenate(rows)

    def per_take(self, q):
        out = {}
        for set_name, takes in self.takes.items():
            for take in takes:
                g = self.gain(take, q)
                if set_name == "history":
                    for grp, e in zip(take["groups"], history_groups(take, g)):
                        out[f"{take['name']} {grp['level']} {grp['history']}"] = e
                else:
                    out[take["name"]] = rms(errors(take, g))
        return out

    def report(self, q, label):
        per = self.per_take(q)
        parts = []
        for set_name in ("older", "history", "reamp"):
            v = np.array([e for k, e in per.items() if k.startswith(("2026", "history", "reamp")[
                ("older", "history", "reamp").index(set_name)])])
            parts.append(f"{set_name} med {np.median(v):.2f} mean {v.mean():.2f} max {v.max():.2f}")
        r05 = [per[k] for k in per if "reamp/threshold-0500" in k]
        print(f"{label:30s} " + " | ".join(parts) + f" | reamp 5:00 {' '.join(f'{e:.2f}' for e in r05)}", flush=True)
        return per

    def bounds(self):
        lo, hi = list(EXT_LO), list(EXT_HI)
        n_maps = sum(m in self.maps for m in ("switch", "slope"))
        return np.r_[lo, [-15.0] * 5 * n_maps], np.r_[hi, [15.0] * 5 * n_maps]

    def start(self, p=EXT_P0):
        n_maps = sum(m in self.maps for m in ("switch", "slope"))
        return np.r_[p, [0.0] * 5 * n_maps]

    def fit(self, q0, weights=SET_WEIGHT, label="fit", fixed=()):
        from scipy import optimize
        lo, hi = self.bounds()
        free = np.array([i not in fixed for i in range(len(q0))])

        def full(x):
            q = np.array(q0, float)
            q[free] = x
            return q

        x0 = np.clip(np.asarray(q0, float)[free], lo[free] + 1e-9, hi[free] - 1e-9)
        r = optimize.least_squares(lambda x: self.residuals(full(x), weights), x0, bounds=(lo[free], hi[free]),
                                   x_scale="jac", diff_step=1e-3, max_nfev=300)
        q = full(r.x)
        self.report(q, label)
        return q


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "score"
    if cmd == "check":
        check()
    elif cmd == "sweep":
        sweep()
    else:
        law, p = LAWS[sys.argv[2] if len(sys.argv) > 2 else "baseline"]
        score(law, p, verbose=True)
