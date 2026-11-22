"""Fit the RT-2's clipper: y = L·(s(G·u + b) − s(b)) / (G·s'(b)), s(v) = v / (1 + |v|^p)^(1/p) (p→∞: a hard clip), per
Mode and Drive position, to the steps' fundamental (absolute, one calibration from Mode I's linear 7:00) and the odd/even
harmonic power re the fundamental (drive_harmonics.py's output: the rotors' response divided out). u: the sine's peak re the
guitar's, after the input clip (+2.25 dB)."""
import json, numpy as np, scipy.optimize
D = json.load(open("build-rt2/fit/drive_harm.json"))
CEIL = 10 ** (2.25 / 20)
N = 2048
ph = np.sin(2 * np.pi * np.arange(N) / N)

def s(v, p): return v / (1 + np.abs(v) ** p) ** (1 / p)
def ds0(b, p, e=1e-4): return (s(b + e, p) - s(b - e, p)) / (2 * e)

def harmonics(a, G, L, p, b):
    u = np.clip(a * ph, -CEIL, CEIL)
    y = L * (s(G * u + b, p) - s(b, p)) / (G * ds0(b, p))
    H = np.abs(np.fft.rfft(y)) / (N / 2)
    return H[1:10]

def features(h):
    h1 = 20 * np.log10(h[0] + 1e-12)
    odd = 10 * np.log10(np.sum(h[2::2] ** 2) + 1e-12) - h1    # H3, H5, H7, H9
    even = 10 * np.log10(np.sum(h[1::2] ** 2) + 1e-12) - h1   # H2, H4, H6, H8
    return h1, max(odd, -60), max(even, -60)

def measured(name):
    d = D[name]; out = []
    for lv, row in zip(d["levels"], d["harm"]):
        r = np.array(row); p1 = r[0]
        pw = 10 ** (r / 10)
        odd = 10 * np.log10(pw[2::2].sum()) - p1; even = 10 * np.log10(pw[1::2].sum()) - p1
        out.append((lv, p1, max(odd, -60), max(even, -60)))
    return out

# Calibration: Mode I at 7:00 is linear: H1 measured − input level.
lin = measured("m1-slow-steps")
c = np.median([p1 - lv for lv, p1, _, _ in lin if lv <= 0])
print(f"calibration {c:.2f} dB (spread {np.std([p1 - lv for lv, p1, _, _ in lin if lv <= 0]):.2f})")

def loss(params, rows, shared):
    G_db, L_db = params[:2]
    p, b = shared
    e = []
    for lv, p1, odd, even in rows:
        h1, o, ev = features(harmonics(10 ** (lv / 20), 10 ** (G_db / 20), 10 ** (L_db / 20), p, b))
        e += [(h1 + c - p1) / 1.5, (o - odd) / 4.0]
        if b != 0 and lv > -24:
            e.append((ev - even) / 6.0)
    return np.array(e)

def fit_mode(names, bias):
    best = None
    for p in (2.5, 4, 6, 10, 20):
        for b in ([0.0] if not bias else [0.1, 0.25, 0.4]):
            total, sols = 0.0, {}
            for n in names:
                rows = measured(n)
                r = min((scipy.optimize.least_squares(loss, x0, args=(rows, (p, b))) for x0 in ([10, 0], [25, 0], [35, 5])),
                        key=lambda r: r.cost)
                total += r.cost; sols[n] = r.x
            if best is None or total < best[0]:
                best = (total, p, b, sols)
    return best

for mode, names, bias in (("I", ["m1-drive-0900-steps", "m1-drive-1200-steps", "m1-drive-0300-steps", "m1-drive-0500-steps"], False),
                          ("III", ["m3-drive-0700-steps", "m3-drive-0900-steps", "m3-drive-1200-steps", "m3-drive-0300-steps", "m3-drive-0500-steps"], True)):
    total, p, b, sols = fit_mode(names, bias)
    print(f"Mode {mode}: knee p = {p}, bias {b}, cost {total:.1f}")
    for n in names:
        G_db, L_db = sols[n]
        print(f"  {n:22s} G {G_db:+6.1f} dB  L {L_db:+5.1f} dB")
        for lv, p1, odd, even in measured(n):
            h1, o, ev = features(harmonics(10 ** (lv / 20), 10 ** (G_db / 20), 10 ** (L_db / 20), p, b))
            print(f"     {lv:+4d}: H1 {p1 - c:+6.1f}/{h1:+6.1f}  odd {odd:+5.0f}/{o:+5.0f}  even {even:+5.0f}/{ev:+5.0f}   (pedal/fit)")
