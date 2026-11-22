#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "Common.h"

namespace namx
{

// The Boss TR-2 Tremolo's gain, a grey-box model fitted to captures of a real one (the study,
// its data and the fits are in captures/boss/tr-2/README.md; tools/capture/fit_tr2.py and
// tools/capture/tr2/candidate.py reproduce the numbers).
//
//   tri  = 1 − 4·min(φ, 1 − φ)                   triangle LFO, +1 at φ = 0
//   s    = max(G·(tri + β), rail)                G from the Wave knob and the rate; a rounded max
//   v    = u0 + D·(s − κ)                        D from Depth
//   c    = soft clamp of v to [0, 1]             softplus(k·v) − softplus(k·(v − 1)), / k
//   g    = one-pole follower of c: τ_down while falling, τ_up while rising
//   gain = T·g                                   T, the top level, from Depth
//   and the audio itself gets a first-order 70 Hz low cut.
//
// Wave is a plain gain on an offset triangle: a small G stays inside the clamp's window
// (a triangle), a large one overshoots both edges (a square). Depth pulls the swing
// towards u0, above the clamp's top, so the gain sits at full level at Depth 0. The gain
// falls ~2.5× faster than it recovers, which is what makes fast settings creep back up
// to the top.
//
// 2026-10-26: u0, β, κ, k, the τ's and the knob maps up to 12:00, from the supply session
// (chosen by ear over the battery one). 2026-11-09, from the focused reamp (the gain doesn't
// depend on what is played), the edges session and October's together, 33 takes, median error
// 1.4 % of full gain, worst take 2.5 %:
//   - the rail: with Wave up and Depth down the pedal's dip has a floor (0.34 of full gain at
//     Depth 12:00); without it the model dipped to silence there, its largest error (11.5 %);
//   - how square Wave gets falls with the rate, top_gain / (1 + f / corner_hz): the old map had
//     one value, read at 5 Hz where the follower hides it (slow squares 1.4× too soft).
//
// The knobs are ours, not the pedal's pot (the user, 2026-11-09: a knob is a plain curve whose
// ends are the pedal's; where on its travel a sound sits is the pot's business, and the middle
// positions were set by eye anyway): Wave runs log from the pedal's least square to its most at
// the current rate (the pedal's own knob does nothing past ~3:30); Depth runs linear in D, the
// top level following the pedal's path (T against D). `wave_from_pedal` and `depth_from_pedal`
// turn a position on the pedal's knobs into ours: for settings saved before, and for captures.
//
// Mono and allocation-free; the caller keeps one Follower per gain curve and one LowCut per
// channel.
namespace tr2
{

constexpr double LOW_CUT_HZ = 70.0;

struct Voicing
{
    float u0;
    float beta;
    float kappa;
    float sharpness;
    double tau_down_s;
    double tau_up_s;
    float rail; // the squared wave's floor, before Depth
    float rail_sharpness;
    float wave_min; // G at Wave's bottom (the pedal's 7:00)
    float top_gain; // G at Wave's top: top_gain / (1 + f / corner_hz)
    float corner_hz;
    // The pedal's own knobs at 7:00, 9:30, 12:00, 2:30 and 5:00 (0, 25, 50, 75, 100 % of the
    // rotation; the middle positions were set by eye). `depth` and `top` together are the path
    // Depth follows: T against D.
    std::array<float, 3> pedal_wave; // G at 7:00, 9:30, 12:00
    float pedal_wave_share_0230; // above 12:00: this far from the 12:00 value to the top at 2:30
    std::array<float, 5> depth;
    std::array<float, 5> top; // gain at the top of the cycle
};

// The capture on a 9 V supply (the pedal as designed): the top of the cycle drops to unity as
// Depth nears maximum. Chosen over the first capture's voicing (a tired battery, ~0.45 dB hot
// on the tops at full Depth; git history, e1eaa67) after an A/B on 2026-10-26.
constexpr Voicing SUPPLY{
    1.10998f,
    0.43044f,
    0.80716f,
    32.3874f,
    0.00352,
    0.00921,
    -0.8342f,
    32.0f,
    0.6086f,
    7.224f,
    6.964f,
    {0.6086f, 0.8973f, 1.4543f},
    0.585f,
    {0.0278f, 0.1515f, 0.4603f, 0.7878f, 1.0f},
    {1.0452f, 1.0578f, 1.0690f, 1.0410f, 0.9952f},
};

// How square Wave gets at its top, at an LFO rate.
inline float wave_top(const Voicing& m, float rate_hz) {
    return m.top_gain / (1.0f + std::max(rate_hz, 0.0f) / m.corner_hz);
}

// The Wave knob at `x` (0–1): G, log from the least square to the most at this rate.
inline float wave_gain(const Voicing& m, float x, float rate_hz) {
    return m.wave_min * std::pow(wave_top(m, rate_hz) / m.wave_min, std::clamp(x, 0.0f, 1.0f));
}

// The Depth knob at `x` (0–1): D, linear from the pedal's least to its most.
inline float depth(const Voicing& m, float x) {
    return m.depth.front() + std::clamp(x, 0.0f, 1.0f) * (m.depth.back() - m.depth.front());
}

// The top level at depth D: along the pedal's path.
inline float top(const Voicing& m, float d) {
    size_t i = 1;
    while (i < m.depth.size() - 1 && d > m.depth[i]) {
        ++i;
    }
    const float t = std::clamp((d - m.depth[i - 1]) / (m.depth[i] - m.depth[i - 1]), 0.0f, 1.0f);
    return m.top[i - 1] + t * (m.top[i] - m.top[i - 1]);
}

// A position on the pedal's own Wave knob (0–1 of its rotation) as our Wave knob (0–1), at an
// LFO rate: the same G.
inline float wave_from_pedal(const Voicing& m, float position, float rate_hz) {
    const float p = std::clamp(position, 0.0f, 1.0f);
    const float top_g = wave_top(m, rate_hz);
    float g;
    if (p <= 0.5f) {
        const float x = p * 4.0f;
        const size_t i = std::min(static_cast<size_t>(x), size_t{1});
        g = m.pedal_wave[i] + (x - static_cast<float>(i)) * (m.pedal_wave[i + 1] - m.pedal_wave[i]);
    } else {
        const float share = p <= 0.75f ? (p - 0.5f) * 4.0f * m.pedal_wave_share_0230
                                       : m.pedal_wave_share_0230 +
                                             (p - 0.75f) * 4.0f * (1.0f - m.pedal_wave_share_0230);
        g = m.pedal_wave[2] + share * (top_g - m.pedal_wave[2]);
    }
    return std::clamp(std::log(g / m.wave_min) / std::log(top_g / m.wave_min), 0.0f, 1.0f);
}

// A position on the pedal's own Depth knob as our Depth knob: the same D (and so the same T).
inline float depth_from_pedal(const Voicing& m, float position) {
    const float x = std::clamp(position, 0.0f, 1.0f) * 4.0f;
    const size_t i = std::min(static_cast<size_t>(x), size_t{3});
    const float d = m.depth[i] + (x - static_cast<float>(i)) * (m.depth[i + 1] - m.depth[i]);
    return (d - m.depth.front()) / (m.depth.back() - m.depth.front());
}

// The clamped control for LFO phase `phase` (0–1): the gain the follower moves toward.
inline float target(const Voicing& m, float phase, float wave_gain, float depth) {
    const float tri = 1.0f - 4.0f * std::min(phase, 1.0f - phase);
    // The rail: rail + softplus(r·(s − rail)) / r; past 30 the softplus is its argument.
    const float z = m.rail_sharpness * (wave_gain * (tri + m.beta) - m.rail);
    const float shaped = m.rail + (z > 30.0f ? z : std::log1p(std::exp(z))) / m.rail_sharpness;
    const float v = m.u0 + depth * (shaped - m.kappa);
    // softplus(s·v) − softplus(s·(v − 1)) = ln((1 + e^(s·v)) / (1 + e^(s·v − s))). Past
    // |s·v| = 40 the clamp is flat to float precision; clamping there keeps e^x finite.
    const float sv = std::clamp(m.sharpness * v, -40.0f, 40.0f);
    const float e = std::exp(sv);
    return std::log((1.0f + e) / (1.0f + e * std::exp(-m.sharpness))) / m.sharpness;
}

// Per-sample coefficients for the follower and the low cut at one sample rate.
struct Coefficients
{
    float down = 0.0f;
    float up = 0.0f;
    float low_cut = 1.0f;

    void prepare(const Voicing& m, double sr) {
        down = static_cast<float>(1.0 - std::exp(-1.0 / (m.tau_down_s * sr)));
        up = static_cast<float>(1.0 - std::exp(-1.0 / (m.tau_up_s * sr)));
        low_cut = static_cast<float>(1.0 / (1.0 + 2.0 * std::numbers::pi * LOW_CUT_HZ / sr));
    }
};

// Follows the clamped control; the caller scales by the voicing's top level.
struct Follower
{
    float g = 1.0f;

    float step(float target_gain, const Coefficients& k) {
        g += (target_gain < g ? k.down : k.up) * (target_gain - g);
        return g;
    }
};

// First-order high-pass (y = a·(y₁ + x − x₁)).
struct LowCut
{
    float x1 = 0.0f;
    float y1 = 0.0f;

    float process(float x, const Coefficients& k) {
        y1 = k.low_cut * (y1 + x - x1);
        x1 = x;
        return y1;
    }
};

} // namespace tr2

} // namespace namx
