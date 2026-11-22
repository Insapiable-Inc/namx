#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>

#include "Common.h"

namespace namx::ge7
{

// The Equalizer: a Boss GE-7 Equalizer fitted to captures of the real pedal
// (captures/boss/ge-7/README.md, tools/capture/analyze_ge7.py — 60 sweeps within 0.23 dB rms;
// on guitar, 0.1–0.3 dB against 0.14 for the pedal twice: tools/capture/ge7/).
//
// The pedal is the classic one-op-amp graphic EQ: seven slider pots between the op-amp's two
// inputs, each wiper into a series resonator to ground. With the inputs at one voltage V,
//   Vin  = V · (1 + R · Σ (1 − x) · B(V))
//   Vout = V · (1 + R · Σ x · B(V))
// x = a slider's wiper (0 = cut end, 1 = boost end), B = 1 / (Z + x(1 − x)) its branch's
// admittance (impedances relative to the pot). For the six LC bands B is a band-pass whose Q
// falls with x(1 − x) — the bands narrow as they're pushed; the top slider has an RC instead
// (a high shelf). The bands interact through the shared V, as in the pedal.
//
// The controls are ours, not the pedal's panel (2026-11-09: its printed marks aren't the dB
// they say, and most of a slider's effect sits next to its centre):
//   - a band's slider is in dB: what the band does at its own frequency (the shelf: far above
//     it) with the other sliders at 0 — `design` finds the wiper that gives it;
//   - every band reaches ±15 dB. The pedal's reach 12.5–15.6: each branch's series resistance
//     is set so its end of travel is 15 (below; the pedal's fitted values beside them). Up to
//     ±12 dB a band stays within 0.1 dB of the pedal's circuit at every frequency;
//   - each band's frequency can move an octave either way (its sqrt(L / C) kept: the same Q);
//   - Level is its gain in dB.
// Sliders saved on the pedal's printed scale (presets and sessions before this) are converted
// by `printed_mark_db`.
//
// Each B is a zero-delay-feedback filter (Simper's SVF, the shelf a one-pole), so its output
// is affine in the current V: the loop is solved per sample in closed form, then the filters
// advance. Digital warping of the bands' skirts: within 0.4 dB of the circuit below 5 kHz,
// up to ~1.4 dB at 8–12 kHz with mixed settings.
//
// Then Level (its top end rolls off over GBW / gain). The pedal's ceiling isn't modelled: its
// op-amps clip a little over a guitar's peak (+1.8 dB on steady sines, +2.2 to +2.6 / −3.0 on
// guitar, 1.7 dB lower on a battery) — a 9 V supply's limit, not the equalizer's sound, and
// in NAM-X the level it would sit at is a guess (removed 2026-11-09, the user).

// The bands: six peaks, then the shelf. The sliders: the bands, then Level.
constexpr size_t BAND_COUNT = 7;
constexpr size_t SLIDER_COUNT = BAND_COUNT + 1;
constexpr size_t SHELF = 6;
constexpr double SLIDER_MIN = -15.0, SLIDER_MAX = 15.0; // dB

constexpr double R = 0.336477;

struct Resonator
{
    double hz;
    double z0; // sqrt(L / C)
    double rs; // series resistance: every band reaches ±15 dB
    double pedal_rs; // as fitted to the pedal
};
constexpr std::array<Resonator, 6> RESONATORS{{
    {91.073, 0.103959, 0.0377153, 0.0350521},
    {195.793, 0.104971, 0.0319805, 0.0354248},
    {369.663, 0.114781, 0.0298748, 0.0355095},
    {728.969, 0.123768, 0.0291675, 0.0368241},
    {1529.48, 0.100821, 0.0299833, 0.0355075},
    {3280.54, 0.121532, 0.0326445, 0.0375891},
}};
// The shelf's RC: its reactance is SHELF_REACTANCE_HZ / f (relative to the pot) with the
// frequency control at SHELF_HZ — the pedal's slider says 6.4 kHz, and that is where the
// shelf reaches half its dB.
constexpr double SHELF_HZ = 6400.0;
constexpr double SHELF_REACTANCE_HZ = 323.428;
constexpr double SHELF_RS = 0.0727768, SHELF_PEDAL_RS = 0.0831582;

// A band's frequency control: its default (the pedal's), an octave either way.
constexpr std::array<double, BAND_COUNT> BAND_HZ{
    RESONATORS[0].hz, RESONATORS[1].hz, RESONATORS[2].hz, RESONATORS[3].hz,
    RESONATORS[4].hz, RESONATORS[5].hz, SHELF_HZ,
};
constexpr double FREQUENCY_SPAN = 2.0;

constexpr double LEVEL_GBW_HZ = 105543.0;

constexpr double WIPER_END = 1e-6; // how close a wiper gets to an end of its pot

inline double db_to_linear(double db) {
    return std::pow(10.0, db / 20.0);
}

// The circuit in the frequency domain, for the sliders' dB scale: what a band does at its
// own frequency with the other wipers centred.
struct Circuit
{
    std::array<double, BAND_COUNT> hz = BAND_HZ;
    std::array<double, BAND_COUNT> rs{
        RESONATORS[0].rs, RESONATORS[1].rs, RESONATORS[2].rs, RESONATORS[3].rs,
        RESONATORS[4].rs, RESONATORS[5].rs, SHELF_RS,
    };

    // A branch's series impedance at f.
    std::complex<double> impedance(size_t band, double f) const {
        if (band == SHELF) {
            return {rs[band], -SHELF_REACTANCE_HZ * hz[band] / SHELF_HZ / f};
        }
        return {rs[band], RESONATORS[band].z0 * (f / hz[band] - hz[band] / f)};
    }

    // 1 + what the other branches, centred, add to both sides at `band`'s own frequency (the
    // shelf's is far above every band: nothing).
    std::complex<double> others(size_t band) const {
        std::complex<double> sum{1.0, 0.0};
        if (band == SHELF) {
            return sum;
        }
        for (size_t j = 0; j < BAND_COUNT; ++j) {
            if (j != band) {
                sum += 0.5 * R / (impedance(j, hz[band]) + 0.25);
            }
        }
        return sum;
    }

    // The band's gain (dB) at its own frequency with its wiper at x, given `others(band)`.
    double gain_db(size_t band, double x, std::complex<double> rest) const {
        const double b = R / (rs[band] + x * (1.0 - x));
        return 20.0 * std::log10(std::abs(rest + x * b) / std::abs(rest + (1.0 - x) * b));
    }

    // The wiper that gives `db`: the gain rises with x and mirrors about the centre.
    double wiper(size_t band, double db) const {
        const std::complex<double> rest = others(band);
        const double target = std::abs(db);
        double lo = 0.5, hi = 1.0 - WIPER_END;
        for (int i = 0; i < 40; ++i) {
            const double mid = 0.5 * (lo + hi);
            (gain_db(band, mid, rest) < target ? lo : hi) = mid;
        }
        return db < 0.0 ? 1.0 - lo : lo;
    }
};

// A slider saved on the pedal's printed scale (−15…+15), as this model's slider: the dB the
// pedal's band gave at that mark with the others at 0; Level's gain at the mark.
inline double printed_mark_db(size_t slider, double mark) {
    static constexpr std::array<double, 9> MARKS{-15.0, -10.0, -7.5, -5.0, 0.0,
                                                 5.0,   7.5,   10.0, 15.0};
    static constexpr std::array<double, 9> BAND_WIPER{0.0,     0.035153, 0.059777, 0.130915, 0.5,
                                                      0.92429, 0.965621, 0.984501, 1.0};
    static constexpr std::array<double, 9> LEVEL_DB{-14.74, -13.41, -11.5, -10.77, 0.0,
                                                    8.63,   11.67,  12.75, 14.73};
    // Piecewise linear between the printed marks.
    auto at_mark = [&](const std::array<double, 9>& values) {
        const double m = std::clamp(mark, MARKS.front(), MARKS.back());
        size_t i = 1;
        while (i < MARKS.size() - 1 && m > MARKS[i]) {
            ++i;
        }
        const double t = (m - MARKS[i - 1]) / (MARKS[i] - MARKS[i - 1]);
        return values[i - 1] + t * (values[i] - values[i - 1]);
    };
    if (slider >= BAND_COUNT) {
        return at_mark(LEVEL_DB);
    }
    Circuit pedal;
    for (size_t k = 0; k < RESONATORS.size(); ++k) {
        pedal.rs[k] = RESONATORS[k].pedal_rs;
    }
    pedal.rs[SHELF] = SHELF_PEDAL_RS;
    const double x = std::clamp(at_mark(BAND_WIPER), WIPER_END, 1.0 - WIPER_END);
    return std::clamp(pedal.gain_db(slider, x, pedal.others(slider)), SLIDER_MIN, SLIDER_MAX);
}

struct Coefficients
{
    struct Svf
    {
        double k = 1, a1 = 1, a2 = 0, a3 = 0; // Simper's SVF; band-pass = k · v1
        double cut = 0, boost = 0; // R (1 − x) / r and R x / r
    };
    std::array<Svf, 6> svf{};
    double shelf_g = 0; // one-pole high-pass: g / (1 + g)
    double shelf_cut = 0, shelf_boost = 0;
    double level = 1;
    double level_lp = 1; // one-pole low-pass g / (1 + g); 1 = off
};

// `sliders`: the seven bands and Level, dB (−15…+15); `hz`: the bands' frequencies.
inline Coefficients design(const std::array<double, SLIDER_COUNT>& sliders,
                           const std::array<double, BAND_COUNT>& hz, double fs) {
    Coefficients c;
    Circuit circuit;
    for (size_t k = 0; k < BAND_COUNT; ++k) {
        circuit.hz[k] = std::clamp(hz[k], BAND_HZ[k] / FREQUENCY_SPAN, BAND_HZ[k] * FREQUENCY_SPAN);
    }
    auto wiper = [&](size_t band) {
        return circuit.wiper(band, std::clamp(sliders[band], SLIDER_MIN, SLIDER_MAX));
    };
    const double nyquist_guard = 0.49 * std::numbers::pi;
    for (size_t k = 0; k < RESONATORS.size(); ++k) {
        const Resonator& res = RESONATORS[k];
        const double x = wiper(k);
        const double r = res.rs + x * (1.0 - x);
        Coefficients::Svf& f = c.svf[k];
        f.k = r / res.z0; // 1 / Q
        const double g = std::tan(std::min(std::numbers::pi * circuit.hz[k] / fs, nyquist_guard));
        f.a1 = 1.0 / (1.0 + g * (g + f.k));
        f.a2 = g * f.a1;
        f.a3 = g * f.a2;
        f.cut = R * (1.0 - x) / r;
        f.boost = R * x / r;
    }
    const double x = wiper(SHELF);
    const double r = SHELF_RS + x * (1.0 - x);
    const double corner_hz = SHELF_REACTANCE_HZ * circuit.hz[SHELF] / SHELF_HZ / r;
    const double g = std::tan(std::min(std::numbers::pi * corner_hz / fs, nyquist_guard));
    c.shelf_g = g / (1.0 + g);
    c.shelf_cut = R * (1.0 - x) / r;
    c.shelf_boost = R * x / r;

    c.level = db_to_linear(std::clamp(sliders[BAND_COUNT], SLIDER_MIN, SLIDER_MAX));
    const double level_hz = LEVEL_GBW_HZ / std::max(1.0, c.level); // the stage's noise gain
    if (level_hz < 0.45 * fs) {
        const double gl = std::tan(std::numbers::pi * level_hz / fs);
        c.level_lp = gl / (1.0 + gl);
    }
    return c;
}

struct State
{
    std::array<double, 6> ic1{}, ic2{};
    double shelf = 0;
    double level_lp = 0;

    void reset() {
        *this = State{};
    }
};

inline float process(const Coefficients& c, State& s, float in) {
    // The loop: every branch's output is G · V + S (S from its state); solve for V.
    double num = in;
    double den = 1.0;
    for (size_t k = 0; k < c.svf.size(); ++k) {
        const Coefficients::Svf& f = c.svf[k];
        num -= f.cut * f.k * (f.a1 * s.ic1[k] - f.a2 * s.ic2[k]);
        den += f.cut * f.k * f.a2;
    }
    num += c.shelf_cut * (1.0 - c.shelf_g) * s.shelf;
    den += c.shelf_cut * (1.0 - c.shelf_g);
    const double v = num / den;

    double out = v;
    for (size_t k = 0; k < c.svf.size(); ++k) {
        const Coefficients::Svf& f = c.svf[k];
        const double v3 = v - s.ic2[k];
        const double v1 = f.a1 * s.ic1[k] + f.a2 * v3;
        const double v2 = s.ic2[k] + f.a2 * s.ic1[k] + f.a3 * v3;
        s.ic1[k] = 2.0 * v1 - s.ic1[k];
        s.ic2[k] = 2.0 * v2 - s.ic2[k];
        out += f.boost * f.k * v1;
    }
    const double step = (v - s.shelf) * c.shelf_g;
    const double low = step + s.shelf;
    s.shelf = low + step;
    out += c.shelf_boost * (v - low);

    out *= c.level;
    if (c.level_lp < 1.0) {
        const double lp_step = (out - s.level_lp) * c.level_lp;
        const double lp = lp_step + s.level_lp;
        s.level_lp = lp + lp_step;
        out = lp;
    }
    return static_cast<float>(out);
}

} // namespace namx::ge7
