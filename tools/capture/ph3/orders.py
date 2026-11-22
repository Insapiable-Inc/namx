#!/usr/bin/env python3
"""How many stages does the PH-3's Rise or Fall need? Each tone's response over the sweep cycle is a series
in u = exp(j*2*pi*t/T) (`cycle.py`); here it is fitted with polynomials and ratios of polynomials in u of
several degrees, [numerator / denominator], and the relative error of each is printed per tone. A degree in
the numerator is a stage (a dry + shifted mix), one in the denominator a loop.

  ph3/orders.py TAKE [TAKE ...]      e.g. a-res700-rise a-res1200-rise a-res500-rise

Read 2026-11-13: [3/3] reaches the takes' floor (0.1-0.8 %) at every tone and Res; with Res at its minimum
a cubic [3/0] is within 0.5-2 % (three stages, the loops nearly shut); two poles and two zeros [2/2] leave
up to 11 % at 3 kHz and 7 % at 5 kHz with Res up. Run from the repository root."""
import pathlib, sys
import numpy as np, scipy.optimize
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import cycle

FORMS = [(2, 0), (3, 0), (2, 2), (3, 2), (3, 3)]
U = np.exp(2j * np.pi * np.arange(256) / 256)


def error(h, top, bottom):
    """The relative error of the best [top / bottom] ratio in u against the terms h0, h1, ..."""
    count = len(h)

    def series(c):
        den = 1 + sum(c[top + 1 + j] * U ** (j + 1) for j in range(bottom))
        return np.fft.fft(sum(c[j] * U ** j for j in range(top + 1)) / den)[:count] / len(U)

    rows = []
    for k in range(count):                       # a linear start: h Q = P term by term
        row = np.zeros(top + 1 + bottom, complex)
        if k <= top:
            row[k] = 1
        for j in range(1, bottom + 1):
            if k - j >= 0:
                row[top + j] = -h[k - j]
        rows.append(row)
    start = np.linalg.lstsq(np.array(rows), h, rcond=None)[0]
    weight = 1 / np.sqrt(np.sum(np.abs(h) ** 2))
    fit = scipy.optimize.least_squares(lambda x: ((series(x.view(complex)) - h) * weight).view(float),
                                       start.view(float), max_nfev=300)
    return np.sqrt(np.sum(np.abs(series(fit.x.view(complex)) - h) ** 2)) * weight


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    print("relative error per form:      " + "  ".join(f"[{m}/{n}]" for m, n in FORMS))
    for name in sys.argv[1:]:
        h, period = cycle.series(name)
        for i in range(1, 9):
            print(f"  {name:16s} {cycle.TONES[i]:5.0f} Hz  " + "  ".join(f"{error(h[i], m, n):5.3f}" for m, n in FORMS))


if __name__ == "__main__":
    main()
