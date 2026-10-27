#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "Common.h"

namespace namx
{

// The Boss TR-2 Tremolo's gain, a grey-box model fitted to captures of a real one
// (2026-10-26; the study, its data and the fits are in captures/boss/tr-2/README.md,
// tools/capture/fit_tr2.py reproduces the numbers).
//
//   tri  = 1 − 4·min(φ, 1 − φ)                   triangle LFO, +1 at φ = 0
//   v    = u0 + D·(G·(tri + β) − κ)              G from the Wave knob, D from Depth
//   c    = soft clamp of v to [0, 1]             softplus(s·v) − softplus(s·(v − 1)), / s
//   g    = one-pole follower of c: τ_down while falling, τ_up while rising
//   gain = T·g                                   T, the top level, from Depth
//   and the audio itself gets a first-order 70 Hz low cut.
//
// Wave is a plain gain on an offset triangle: a small G stays inside the clamp's window
// (a triangle), a large one overshoots both edges (a square). Depth pulls the swing
// towards u0, above the clamp's top, so the gain sits at full level at Depth 0. The gain
// falls ~2.5× faster than it recovers, which is what makes fast settings creep back up
// to the top. Mono and allocation-free; the caller keeps one Follower per gain curve and
// one LowCut per channel.
namespace tr2
{

constexpr double LOW_CUT_HZ = 70.0;

// One fit of the model. Knob maps at 7:00, 9:30, 12:00, 2:30 and 5:00 (0, 25, 50, 75,
// 100 % of the rotation), linear in between; the middle positions were set by eye.
struct Voicing
{
    float u0;
    float beta;
    float kappa;
    float sharpness;
    double tau_down_s;
    double tau_up_s;
    std::array<float, 5> wave_gain;
    std::array<float, 5> depth;
    std::array<float, 5> top; // gain at the top of the cycle
};

// The first capture, on a tired battery (8.37 V): approved by ear 2026-10-26, so its
// numbers stay as they were (one top level for every Depth: the battery held it flat).
constexpr Voicing BATTERY{
    1.25425f,
    0.42844f,
    0.96141f,
    18.3909f,
    0.00346,
    0.00866,
    {0.6422f, 1.1268f, 1.4826f, 2.7587f, 3.4550f},
    {0.1432f, 0.2218f, 0.4937f, 0.7928f, 1.0f},
    {1.05787f, 1.05787f, 1.05787f, 1.05787f, 1.05787f},
};

// The recapture on a 9 V supply (the pedal as designed): the top of the cycle drops to
// unity as Depth nears maximum. Median error 1.3 % of full gain.
constexpr Voicing SUPPLY{
    1.10998f,
    0.43044f,
    0.80716f,
    32.3874f,
    0.00352,
    0.00921,
    {0.6086f, 0.8973f, 1.4543f, 2.9215f, 3.5130f},
    {0.0278f, 0.1515f, 0.4603f, 0.7878f, 1.0f},
    {1.0452f, 1.0578f, 1.0690f, 1.0410f, 0.9952f},
};

// A knob at `fraction` (0–1) of its rotation, through one of the maps above.
inline float knob_map(const std::array<float, 5>& map, float fraction) {
    const float x = std::clamp(fraction, 0.0f, 1.0f) * 4.0f;
    const size_t i = std::min(static_cast<size_t>(x), size_t{3});
    return map[i] + (x - static_cast<float>(i)) * (map[i + 1] - map[i]);
}

// The clamped control for LFO phase `phase` (0–1): the gain the follower moves toward.
inline float target(const Voicing& m, float phase, float wave_gain, float depth) {
    const float tri = 1.0f - 4.0f * std::min(phase, 1.0f - phase);
    const float v = m.u0 + depth * (wave_gain * (tri + m.beta) - m.kappa);
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
