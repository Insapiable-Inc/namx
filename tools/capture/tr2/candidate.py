"""A candidate for the TR-2 model's square settings, fitted on steady-tone gain cycles: the edges
session (2026-11-09-edges), the focused reamp's probe-alone takes and the 2026-10-26 supply session
the frozen voicing came from; two takes of the edges session are held out. The frozen constants and
everything at Wave 12:00 and below stay as they are. The candidate changes two things above it:

  rail     the squared wave stops at this value before Depth scales it. With Wave up and Depth
           down the pedal's dip has a floor (Wave 5:00, Depth 12:00: 0.34 of full gain); the
           frozen model has none and dips to silence.
  top      the Wave map's value at 5:00 falls with the rate: top_gain / (1 + f / corner_hz). The
           frozen map has one value, read at 5 Hz, where the follower hides it: slow squares came
           out too soft, fast ones a little too hard. At 2:30 the map sits `share_0230` of the way
           from its 12:00 value to the top.

Tried and dropped: a control sinking under silence before the follower (once the top is right the
fit sets it to nothing), Wave as a blend of the triangle with a square (the fitted pair still moves
with the rate).

  tr2/candidate.py [--plot]   → build-tr2/candidate.json; with --plot the figures in the edges
                                session's fit/ folder

Run from the repository root."""
import json, pathlib, sys
import numpy as np, scipy.optimize
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import model as M

SESSIONS = {
    "edges": ("captures/boss/tr-2/2026-11-09-edges", "bypass", 9000.0),
    "focused": ("captures/boss/tr-2/2026-11-09-focused", "bypass-chords", 9000.0),
    "october": ("captures/boss/tr-2/2026-10-26-psu", "bypass-sine", 1000.0),
}
FIGURES = pathlib.Path("captures/boss/tr-2/2026-11-09-edges/fit")
V = M.SUPPLY
RAIL_SHARPNESS = 32.0  # as sharp as the frozen clamp's corners
HIGHLIGHTS = ["rate-1200-wave-0500-depth-1200", "held-out-rate-1200-wave-0300-depth-1200", "rate-0700-wave-0500",
              "rate-0900-wave-0500", "rate-0900-wave-0300", "rate-0500-wave-0500",
              "held-out-rate-0300-wave-0300-depth-0300", "rate-0900-wave-1200"]


def load():
    takes = []
    for session, (path, bypass, carrier) in SESSIONS.items():
        for name, (settings, hz, cycle) in M.measured(path, bypass=bypass, carrier=carrier).items():
            takes.append({"session": session, "name": name, "settings": settings, "hz": hz, "cycle": cycle,
                          "held_out": name.startswith("held-out"), "by_eye": settings["Wave"] not in M.PCT})
    return takes


def wave_gain(take, q, share=None):
    """The candidate's Wave gain: the frozen map up to 12:00; above it, a share of the way from the
    12:00 value to the top, which falls with the rate. `share`: for a position set by eye."""
    pct = M.PCT.get(take["settings"]["Wave"])
    if share is None:
        if pct <= 50:
            return M.knob_map(V["wave"], pct)
        share = np.interp(pct, [50, 75, 100], [0, q["share_0230"], 1])
    top = q["top_gain"] / (1 + take["hz"] / q["corner_hz"])
    return V["wave"][2] + share * (top - V["wave"][2])


def model(take, q=None, share=None):
    """The take's cycle, turned onto the measured one: the frozen model (q None) or the candidate."""
    s = take["settings"]
    depth, top = M.knob_map(V["depth"], M.PCT[s["Depth"]]), M.knob_map(V["top"], M.PCT[s["Depth"]])
    if q is None:
        return M.align(take["cycle"], M.cycle(V, M.knob_map(V["wave"], M.PCT[s["Wave"]]), depth, top, take["hz"]))
    candidate = {"under": 0.0, "rail": q["rail"], "rail_sharpness": RAIL_SHARPNESS}
    return M.align(take["cycle"], M.cycle(V, wave_gain(take, q, share), depth, top, take["hz"], candidate=candidate))


def rms(x):
    return float(np.sqrt(np.mean(x ** 2)))


def main():
    takes = load()
    fitted = [t for t in takes if not t["held_out"]]
    eye = [t["name"] for t in fitted if t["by_eye"]]

    def unpack(p):
        return {"rail": p[0], "top_gain": p[1], "corner_hz": p[2], "share_0230": p[3]}, dict(zip(eye, p[4:]))

    def residuals(p):
        q, shares = unpack(p)
        return np.concatenate([model(t, q, shares.get(t["name"])) - t["cycle"] for t in fitted])

    fit = scipy.optimize.least_squares(residuals, [-0.85, 7.4, 7.5, 0.5] + [0.9] * len(eye), diff_step=1e-3,
                                       bounds=([-3, 2, 0.5, 0] + [0] * len(eye), [-0.3, 40, 200, 1] + [1.5] * len(eye)))
    q, shares = unpack(fit.x)
    print(f"rail {q['rail']:.4f}   top {q['top_gain']:.3f} / (1 + f / {q['corner_hz']:.3f} Hz)   share at 2:30 "
          f"{q['share_0230']:.3f}   by eye: " + ", ".join(f"{n.split('-')[-1]} → {s:.2f}" for n, s in shares.items()))
    print(f"\n{'session':8s} {'take':42s}   Hz   frozen rms   max  │ candidate rms  max")
    rows = []
    for t in takes:
        frozen = None if t["by_eye"] else model(t) - t["cycle"]
        new = model(t, q, shares.get(t["name"])) - t["cycle"]
        rows.append((t, frozen, new))
        left = "  (Wave by eye)  " if frozen is None else f"  {rms(frozen):.4f} {np.abs(frozen).max():.4f}"
        print(f"{t['session']:8s} {t['name']:42s} {t['hz']:6.2f} {left}  │    {rms(new):.4f} {np.abs(new).max():.4f}"
              + ("  held out" if t["held_out"] else ""))
    both = [(rms(f), rms(n)) for t, f, n in rows if f is not None]
    print(f"\n{len(both)} takes at marked positions — median rms: frozen {np.median([b[0] for b in both]):.4f}, candidate "
          f"{np.median([b[1] for b in both]):.4f}; worst: frozen {max(b[0] for b in both):.4f}, candidate "
          f"{max(b[1] for b in both):.4f}; worse by more than 0.001: "
          f"{[t['name'] for t, f, n in rows if f is not None and rms(n) > rms(f) + 0.001]}")
    pathlib.Path("build-tr2").mkdir(exist_ok=True)
    json.dump(dict(q, share_by_eye=shares), open("build-tr2/candidate.json", "w"), indent=2)
    if "--plot" in sys.argv:
        FIGURES.mkdir(exist_ok=True)
        plot([r for name in HIGHLIGHTS for r in rows if r[0]["name"] == name and r[0]["session"] == "edges"],
             FIGURES / "tr-2-candidate.png", "the takes that say most")
        for session in SESSIONS:
            plot([r for r in rows if r[0]["session"] == session], FIGURES / f"tr-2-candidate-{session}.png",
                 f"every take of the {session} session")


def plot(rows, path, what):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    columns = 4
    lines = -(-len(rows) // columns)
    fig, axes = plt.subplots(lines, columns, figsize=(4.6 * columns, 3.1 * lines), squeeze=False)
    for ax in axes.flat:
        ax.axis("off")
    for ax, (t, frozen, new) in zip(axes.flat, rows):
        ax.axis("on")
        # Two cycles, so an edge at the fold shows whole.
        ms = np.arange(2 * M.BINS) / M.BINS * 1000 / t["hz"]
        twice = lambda x: np.concatenate([x, x])
        ax.plot(ms, twice(t["cycle"]), color="black", lw=2.4, label="pedal")
        if frozen is not None:
            ax.plot(ms, twice(t["cycle"] + frozen), color="#d55e00", lw=1.3, label=f"NAM-X now (rms {rms(frozen):.3f})")
        ax.plot(ms, twice(t["cycle"] + new), color="#0072b2", lw=1.3, ls="--", label=f"candidate (rms {rms(new):.3f})")
        s = t["settings"]
        ax.set_title(f"Rate {s['Rate']} · Wave {s['Wave']} · Depth {s['Depth']}   ({t['hz']:.1f} Hz)"
                     + ("\nheld out of the fit" if t["held_out"] else ""), fontsize=9)
        ax.set_xlabel("ms", fontsize=8)
        ax.set_ylabel("gain (1 = bypass)", fontsize=8)
        ax.set_ylim(-0.05, 1.15)
        ax.tick_params(labelsize=7)
        ax.legend(fontsize=7, loc="lower left")
        ax.grid(alpha=0.3)
    fig.suptitle(f"Boss TR-2: the gain over two cycles, pedal against NAM-X's model and the candidate — {what}",
                 fontsize=12)
    fig.tight_layout()
    fig.savefig(path, dpi=100)
    plt.close(fig)


if __name__ == "__main__":
    main()
