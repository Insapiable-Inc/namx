#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "Common.h"

namespace namx::ge7
{

// The EQ's GE-7 type: a Boss GE-7 Equalizer fitted to captures of the real pedal
// (captures/boss/ge-7/README.md, tools/capture/analyze_ge7.py — 60 sweeps within 0.23 dB rms).
//
// The pedal is the classic one-op-amp graphic EQ: seven slider pots between the op-amp's two
// inputs, each wiper into a series resonator to ground. With the inputs at one voltage V,
//   Vin  = V · (1 + R · Σ (1 − x) · B(V))
//   Vout = V · (1 + R · Σ x · B(V))
// x = a slider's wiper (0 = cut end, 1 = boost end), B = 1 / (Z + x(1 − x)) its branch's
// admittance (impedances relative to the pot). For the six LC bands B is a band-pass whose Q
// falls with x(1 − x) — the bands narrow as they're pushed; the 6.4 kHz slider has an RC
// instead (a high shelf). The bands interact through the shared V, as in the pedal.
//
// Each B is a zero-delay-feedback filter (Simper's SVF, the shelf a one-pole), so its output
// is affine in the current V: the loop is solved per sample in closed form, then the filters
// advance. Digital warping of the bands' skirts: within 0.4 dB of the circuit below 5 kHz,
// up to ~1.4 dB at 8–12 kHz with mixed settings.
//
// Then Level (its dB per printed mark, measured; its top end rolls off over GBW / gain) and
// one ceiling, +1.8 dB over the guitar's peak whatever the sliders and Level, after each
// stage (re an assumed −6 dBFS post-amp guitar peak, like the other pedal types).

// The sliders: 100, 200, 400, 800 Hz, 1.6, 3.2, 6.4 kHz, then Level.
constexpr size_t BAND_COUNT = 7;
constexpr size_t SLIDER_COUNT = BAND_COUNT + 1;
constexpr double SLIDER_MIN = -15.0, SLIDER_MAX = 15.0; // the pedal's printed scale

constexpr double R = 0.336477;

struct Resonator
{
    double hz;
    double z0; // sqrt(L / C)
    double rs; // series resistance
};
constexpr std::array<Resonator, 6> RESONATORS{{
    {91.073, 0.103959, 0.0350521},
    {195.793, 0.104971, 0.0354248},
    {369.663, 0.114781, 0.0355095},
    {728.969, 0.123768, 0.0368241},
    {1529.48, 0.100821, 0.0355075},
    {3280.54, 0.121532, 0.0375891},
}};
constexpr double SHELF_HZ = 3889.31; // the RC's corner re its own resistance
constexpr double SHELF_RS = 0.0831582;

// The printed marks and what they are: the band sliders' wiper, Level's gain.
constexpr std::array<double, 9> MARKS{-15.0, -10.0, -7.5, -5.0, 0.0, 5.0, 7.5, 10.0, 15.0};
constexpr std::array<double, 9> BAND_WIPER{0.0,     0.035153, 0.059777, 0.130915, 0.5,
                                           0.92429, 0.965621, 0.984501, 1.0};
constexpr std::array<double, 9> LEVEL_DB{-14.74, -13.41, -11.5, -10.77, 0.0,
                                         8.63,   11.67,  12.75, 14.73};
constexpr double LEVEL_GBW_HZ = 105543.0;

constexpr double REFERENCE_DBFS = -6.0; // an assumed post-amp guitar peak
constexpr double CEILING_DB = 1.8; // over it
constexpr double KNEE = 10.0; // the soft clip's exponent: nearly hard, rounded

inline double db_to_linear(double db) {
    return std::pow(10.0, db / 20.0);
}

// Piecewise linear between the printed marks.
inline double at_mark(const std::array<double, 9>& values, double mark) {
    const double m = std::clamp(mark, MARKS.front(), MARKS.back());
    size_t i = 1;
    while (i < MARKS.size() - 1 && m > MARKS[i]) {
        ++i;
    }
    const double t = (m - MARKS[i - 1]) / (MARKS[i] - MARKS[i - 1]);
    return values[i - 1] + t * (values[i] - values[i - 1]);
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
    double ceiling = 1;
};

// `sliders`: the seven bands and Level on the printed scale (−15…+15).
inline Coefficients design(const std::array<double, SLIDER_COUNT>& sliders, double fs) {
    Coefficients c;
    auto wiper = [](double mark) {
        return std::clamp(at_mark(BAND_WIPER, mark), 1e-6, 1.0 - 1e-6);
    };
    const double nyquist_guard = 0.49 * std::numbers::pi;
    for (size_t k = 0; k < RESONATORS.size(); ++k) {
        const Resonator& res = RESONATORS[k];
        const double x = wiper(sliders[k]);
        const double r = res.rs + x * (1.0 - x);
        Coefficients::Svf& f = c.svf[k];
        f.k = r / res.z0; // 1 / Q
        const double g = std::tan(std::min(std::numbers::pi * res.hz / fs, nyquist_guard));
        f.a1 = 1.0 / (1.0 + g * (g + f.k));
        f.a2 = g * f.a1;
        f.a3 = g * f.a2;
        f.cut = R * (1.0 - x) / r;
        f.boost = R * x / r;
    }
    const double x = wiper(sliders[6]);
    const double r = SHELF_RS + x * (1.0 - x);
    const double corner_hz = SHELF_HZ * SHELF_RS / r;
    const double g = std::tan(std::min(std::numbers::pi * corner_hz / fs, nyquist_guard));
    c.shelf_g = g / (1.0 + g);
    c.shelf_cut = R * (1.0 - x) / r;
    c.shelf_boost = R * x / r;

    c.level = db_to_linear(at_mark(LEVEL_DB, sliders[7]));
    const double level_hz = LEVEL_GBW_HZ / std::max(1.0, c.level); // the stage's noise gain
    if (level_hz < 0.45 * fs) {
        const double gl = std::tan(std::numbers::pi * level_hz / fs);
        c.level_lp = gl / (1.0 + gl);
    }
    c.ceiling = db_to_linear(REFERENCE_DBFS + CEILING_DB);
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

// Rounded clip toward ±ceiling; exact (to 0.0004 dB) under half of it.
inline double clip(double v, double ceiling) {
    const double a = std::abs(v) / ceiling;
    if (a < 0.5) {
        return v;
    }
    return v / std::pow(1.0 + std::pow(a, KNEE), 1.0 / KNEE);
}

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

    out = clip(out, c.ceiling) * c.level;
    if (c.level_lp < 1.0) {
        const double lp_step = (out - s.level_lp) * c.level_lp;
        const double lp = lp_step + s.level_lp;
        s.level_lp = lp + lp_step;
        out = lp;
    }
    return static_cast<float>(clip(out, c.ceiling));
}

} // namespace namx::ge7
