#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "Common.h"

namespace namx
{

// The Boss TR-2 Tremolo's gain, a grey-box model fitted to captures of a real one
// (2026-10-26; the study, its data and the fit are in captures/boss/tr-2/README.md,
// tools/capture/fit_tr2.py reproduces the numbers). Median error 1.5 % of full gain
// over 15 measured settings.
//
//   tri  = 1 − 4·min(φ, 1 − φ)                   triangle LFO, +1 at φ = 0
//   v    = U0 + D·(G·(tri + BETA) − KAPPA)       G from the Wave knob, D from Depth
//   c    = soft clamp of v to [0, 1]             softplus(s·v) − softplus(s·(v − 1)), / s
//   g    = one-pole follower of c: TAU_DOWN while falling, TAU_UP while rising
//   gain = TOP · g;  the audio itself gets a first-order 70 Hz low cut
//
// Wave is a plain gain on an offset triangle: a small G stays inside the clamp's window
// (a triangle), a large one overshoots both edges (a square). Depth pulls the swing
// towards U0, above the clamp's top, so the gain sits at full level at Depth 0. The
// gain falls ~2.5× faster than it recovers, which is what makes fast settings creep
// back up to the top. Mono and allocation-free; the caller keeps one Follower per gain
// curve and one LowCut per channel.
namespace tr2
{

constexpr float U0 = 1.25425f;
constexpr float BETA = 0.42844f;
constexpr float KAPPA = 0.96141f;
constexpr float SHARPNESS = 18.3909f;
constexpr float TOP = 1.05787f; // +0.49 dB over bypass at the top of the cycle
constexpr double TAU_DOWN_S = 0.00346;
constexpr double TAU_UP_S = 0.00866;
constexpr double LOW_CUT_HZ = 70.0;

// Knob maps measured at 7:00, 9:30, 12:00, 2:30 and 5:00 (0, 25, 50, 75, 100 % of the
// rotation); linear in between. The middle positions were set by eye (a few degrees).
constexpr std::array<float, 5> WAVE_GAIN{0.6422f, 1.1268f, 1.4826f, 2.7587f, 3.4550f};
constexpr std::array<float, 5> DEPTH{0.1432f, 0.2218f, 0.4937f, 0.7928f, 1.0f};

// A knob at `fraction` (0–1) of its rotation, through one of the maps above.
inline float knob_map(const std::array<float, 5>& map, float fraction) {
    const float x = std::clamp(fraction, 0.0f, 1.0f) * 4.0f;
    const size_t i = std::min(static_cast<size_t>(x), size_t{3});
    return map[i] + (x - static_cast<float>(i)) * (map[i + 1] - map[i]);
}

// The clamped control for LFO phase `phase` (0–1): the gain the follower moves toward.
inline float target(float phase, float wave_gain, float depth) {
    const float tri = 1.0f - 4.0f * std::min(phase, 1.0f - phase);
    const float v = U0 + depth * (wave_gain * (tri + BETA) - KAPPA);
    // softplus(s·v) − softplus(s·(v − 1)) = ln((1 + e^(s·v)) / (1 + e^(s·v − s))). Past
    // |s·v| = 40 the clamp is flat to float precision; clamping there keeps e^x finite.
    const float sv = std::clamp(SHARPNESS * v, -40.0f, 40.0f);
    const float e = std::exp(sv);
    return std::log((1.0f + e) / (1.0f + e * std::exp(-SHARPNESS))) / SHARPNESS;
}

// Per-sample coefficients for the follower and the low cut at one sample rate.
struct Coefficients
{
    float down = 0.0f;
    float up = 0.0f;
    float low_cut = 1.0f;

    void prepare(double sr) {
        down = static_cast<float>(1.0 - std::exp(-1.0 / (TAU_DOWN_S * sr)));
        up = static_cast<float>(1.0 - std::exp(-1.0 / (TAU_UP_S * sr)));
        low_cut = static_cast<float>(1.0 / (1.0 + 2.0 * std::numbers::pi * LOW_CUT_HZ / sr));
    }
};

struct Follower
{
    float g = 1.0f;

    float step(float target_gain, const Coefficients& k) {
        g += (target_gain < g ? k.down : k.up) * (target_gain - g);
        return TOP * g;
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
