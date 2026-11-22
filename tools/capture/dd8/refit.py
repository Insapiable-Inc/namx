"""The DD-8's Analog and Tape loops, refitted (2026-11-10) from the October sessions' sweeps: the
first repeat's response and what one more pass round the loop does to it, read on the deconvolved
sweeps (time-1200-m12, feedback-0900-m12, feedback-1200-m12; -12 dB, under the clip) at sixth
octaves through a 170 ms window — the October fits read the first repeat through 21 ms, which
can't resolve the lows (it read Tape's 250 Hz 2.5 dB high), and the passes at 1, 2 and 4 kHz only.

  first repeat re dry  = repeat level + loop chain + out chain
  one pass             = Feedback   + loop chain + feedback chain     (all unity at 1 kHz)

  dd8/refit.py analog|tape     prints the measurement, the fit and Dd8Delay.h's sections

Run from the repository root. The guitar takes (dd8/repeats.py) are the check, not the fit."""
import pathlib, sys
import numpy as np, scipy.optimize
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import analyze_dd8, fit_voicings

RATE = 48000
F = np.geomspace(40, 10000, 49)  # sixth octaves
ONE_KHZ = int(np.argmin(np.abs(F - 1000)))
WIDTH = 8192
TAKES = ["time-1200-m12", "feedback-0900-m12", "feedback-1200-m12"]


def response(h, at):
    seg = np.take(h, np.arange(at - WIDTH // 4, at - WIDTH // 4 + WIDTH), mode="wrap") * np.hanning(WIDTH)
    power = np.abs(np.fft.rfft(seg, 1 << 17)) ** 2
    f = np.fft.rfftfreq(1 << 17, 1 / RATE)
    edge = 2 ** (1 / 12)
    return np.array([10 * np.log10(power[(f >= c / edge) & (f < c * edge)].mean() + 1e-30) for c in F])


def measure(session):
    """The first repeat re the dry (mean of the takes) and each pass re its 1 kHz (repeat 2 - 1 and
    3 - 2 of the Feedback takes)."""
    first, passes = [], []
    for name in TAKES:
        h, _ = analyze_dd8.impulse_response(session, name)
        dry, repeat = analyze_dd8.first_repeat(h, RATE)
        period, at = repeat - dry, [dry, repeat]
        for _ in range(2):
            guess = at[-1] + period
            at.append(guess - 2000 + int(np.argmax(np.abs(h[guess - 2000:guess + 2000]))))
        levels = [response(h, a) for a in at]
        first.append(levels[1] - levels[0])
        if name.startswith("feedback"):
            passes += [d - d[ONE_KHZ] for d in (levels[2] - levels[1], levels[3] - levels[2])]
    return np.mean(first, axis=0), np.array(passes)


def chain_db(sections):
    if not sections:
        return np.zeros_like(F)
    return fit_voicings.chain_db(sections, F) - fit_voicings.chain_db(sections, np.array([1000.0]))[0]


E = np.exp
MODES = {
    # loop: a low shelf and two low-passes (the hump at 1.6 kHz); feedback: a first-order high-pass
    "analog": (lambda v: ([["low_shelf", E(v[0]), 0.5, v[1]], ["low_pass", E(v[2]), E(v[3]), 0],
                           ["low_pass", E(v[4]), E(v[5]), 0]], [], [["high_pass_1", E(v[6]), 0.707, 0]]),
               np.r_[np.log(2700), 2.0, np.log([3000, 0.75, 2000, 1.3, 100])], (60, 5000)),
    # loop: a low-pass; out: the low cut and a low-pass; feedback: a mid peak, a high-pass, a low-pass
    "tape": (lambda v: ([["low_pass", E(v[0]), E(v[1]), 0]],
                        [["high_pass", E(v[2]), E(v[3]), 0], ["low_pass", E(v[4]), E(v[5]), 0]],
                        [["peak", E(v[6]), E(v[7]), v[8]], ["high_pass_1", E(v[9]), 0.707, 0],
                         ["low_pass_1", E(v[10]), 0.707, 0]]),
             np.r_[np.log([3800, 0.6, 136, 0.55, 11000, 0.9, 1200, 0.8]), 2.3, np.log([47, 7100])], (45, 9000)),
}


def row(label, v):
    return f"{label:24s}" + " ".join(f"{x:+5.1f}" for x in v[::2])


def main():
    mode = sys.argv[1]
    unpack, start, (low, high) = MODES[mode]
    first, passes = measure(fit_voicings.BOSS / "dd-8" / f"2026-10-26-{mode}")
    level = first[ONE_KHZ]
    first, one_pass = first - level, passes.mean(axis=0)
    one_pass -= one_pass[ONE_KHZ]
    band = (F >= low) & (F <= high)
    weight = (1 / (1 + np.maximum(0, -first - 20) / 10))[band]

    def model(v):
        loop, out, feedback = unpack(v)
        return chain_db(loop) + chain_db(out), chain_db(loop) + chain_db(feedback)

    def residual(v):
        a, b = model(v)
        return np.r_[(a - first)[band] * weight, (b - one_pass)[band] * weight]

    fit = scipy.optimize.least_squares(residual, start, x_scale=0.3)
    a, b = model(fit.x)
    print(" " * 24 + " ".join(f"{f:5.0f}" for f in F[::2]))
    print(row("first repeat re 1 kHz", first))
    print(row("  fit - measured", a - first))
    print(row("a pass re 1 kHz", one_pass))
    print(row("  its spread, takes", passes.std(axis=0)))
    print(row("  fit - measured", b - one_pass))
    rms = lambda x: float(np.sqrt(np.mean(x[band] ** 2)))
    print(f"first repeat at 1 kHz {level:+.2f} dB re the dry; fit rms {low}-{high} Hz: first {rms(a - first):.2f} dB, "
          f"pass {rms(b - one_pass):.2f} dB; the pass's peak {b.max():+.2f} dB re 1 kHz at {F[np.argmax(b)]:.0f} Hz")
    for name, sections in zip(("loop", "out", "feedback"), unpack(fit.x)):
        print(f"  .{name} = {{{{")
        for kind, hz, q, gain in sections:
            print(f"      {{FilterKind::{kind.upper()}, {hz:.1f}f, {q:.4f}f, {gain:.2f}f}},")
        print("  }},")


if __name__ == "__main__":
    main()
