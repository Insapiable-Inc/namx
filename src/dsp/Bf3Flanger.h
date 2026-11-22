#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "BiquadChain.h"
#include "Common.h"

namespace namx
{

// The Boss BF-3 Flanger, STANDARD and ULTRA modes, fitted to captures of a real one
// (2026-10-27 on; the study and its data are in captures/boss/bf-3/README.md). A digital
// flanger:
//
//   STANDARD  w    = REFIT_TONE( line(t − d(t)) )             read before the push
//             line = x + r(Res) · w                           this sample's wet feeds the loop
//             out  = clip( DRY · x + WET · w )
//   ULTRA     two of STANDARD's stages in series, the first at d/2, the second at d, each
//             with its own loop at 0.755 of STANDARD's gain; one clip on the output
//   d(t)  = floor + swing(Depth) · s(t)^p(Depth)  (after the dry; p 2.45 to 2.62)
//   floor = (1 − b(Depth)) · m(Manual) + b(Depth) · 0.33 ms
//   s(t)  = a 0–1 triangle at the Rate, through a one-pole low-pass at 1.655 Hz
//   stereo: the same on each side, the right side's sweep an eighth of a cycle ahead (the
//   pedal's OUTPUT B against its OUTPUT A, measured 2026-11-10)
//
// STANDARD is the 2026-10-30 refit (tools/research/BF3_PHASE_FOLLOWUP.html,
// BF3_LOCKED_VALIDATION.html): a three-biquad wet tone fitted to the complex response, so the
// dry/wet sum is right in the bass too. On guitar with the delay standing still (the focused
// session, 2026-11-10) the whole output cancels against the pedal's to −32 to −35 dB; the
// first Standard, whose tone matched the wet's level but not its phase, reached −8 to −14 and
// was removed. Fitted at 48 kHz only (the same frequencies and Qs at other rates shift the top
// octave).
//
// ULTRA (2026-11-10): two flangers in series, (a + b·T·z^(−d/2)) · (a + b·T·z^(−d)) — which is
// the four evenly spaced, nearly equal copies October saw (the dry, d/2, d, 3d/2), the last one
// through the tone twice: later and duller, as measured. Against October's ULTRA sweeps it fits
// better with fewer numbers than three free taps (−19 to −21 dB against −17 to −18), and with
// a loop of its own in each stage it stays there at every Res position (one loop on either
// stage, or one round both: −12 to −17 dB at Res 3:00 and 5:00). The loops' gain is 0.755 of
// STANDARD's at each position (0.109, 0.293, 0.512, 0.674 against 0.146 … 0.89).
// The pedal delays its dry by 0.55 ms (converter latency); here the dry is undelayed and
// the delays are the pedal's dry-to-wet spacings.
namespace bf3
{

// A hard-played guitar's peak, as for the DD-8 and CE-5 (Dd8Delay.h, Ce5Chorus.h): an
// assumption — the clip sits 0.55 dB over the guitar's peak in the study's rig.
constexpr float REFERENCE_DBFS = -6.0f;
constexpr float CEILING_DB = 0.55f; // re REFERENCE_DBFS, on the summed output

constexpr size_t TONE_SECTIONS = 4;

// STANDARD's wet tone, re 1 kHz and then REFIT_TONE_DB (on every pass round the loop too),
// and its own delay compensation (in place of TONE_DELAY_MS).
constexpr FilterChain<TONE_SECTIONS> REFIT_TONE{{
    {FilterKind::HIGH_PASS, 148.322387f, 0.544603f, 0.0f},
    {FilterKind::LOW_PASS, 7899.7738f, 0.405237f, 0.0f},
    {FilterKind::LOW_PASS, 9995.4989f, 1.013955f, 0.0f},
    {FilterKind::NONE, 0.0f, 0.707f, 0.0f},
}};
constexpr float REFIT_TONE_DB = -0.24726f;
constexpr float REFIT_DELAY_MS = 0.052985f;

// Levels at 1 kHz re bypass (re-measured 2026-10-29: each was ~2 dB high, the bypass
// reference read low).
constexpr float STANDARD_DRY_DB = -0.13f;
constexpr float STANDARD_WET_DB = 1.45f;
// ULTRA: each of its two stages, from guitar with the delay standing still (2026-11-10; the
// whole dry −1.0 dB re bypass, as October read it), the d/2 stage's delay short of half of d
// (6 µs: October's sweeps 3–7, the guitar takes 7.5), and its loops' gain re STANDARD's.
constexpr float ULTRA_STAGE_DRY_DB = -0.50f;
constexpr float ULTRA_STAGE_WET_DB = -0.56f;
constexpr float ULTRA_HALF_SHORT_MS = 0.006f;
constexpr float ULTRA_RES_SCALE = 0.755f;

// Knob maps at 7:00, 9:00, 12:00, 3:00, 5:00 (fraction of the rotation), linear in between.
constexpr std::array<float, 5> KNOB_AT{0.0f, 0.2f, 0.5f, 0.8f, 1.0f};

// Manual: the delay after the dry at Depth minimum (ms; log interpolation).
constexpr std::array<float, 5> MANUAL_MS{14.673f, 9.065f, 2.005f, 0.701f, 0.447f};

// Depth: how far the floor moves from Manual's delay toward LOW_MS, and the swing (as at a
// slow Rate; the smoothing takes some off as Rate rises).
constexpr float LOW_MS = 0.33f;
constexpr std::array<float, 5> BLEND{0.0f, 0.368f, 0.657f, 0.882f, 0.984f};
constexpr std::array<float, 5> SWING_MS{0.0f, 1.102f, 4.867f, 10.655f, 14.406f};

// The cycle's shape, refitted 2026-11-10 to a probe tone's phase (the focused session; four
// Rates at Depth 5:00 with one law: 46 µs rms on swings of 1.6 to 13.7 ms, it was 214): the
// triangle through a one-pole low-pass, then to a power that grows with Depth (2.45 fits Depth
// 12:00, 2.62 Depth 5:00; 3:00 placed between).
constexpr float SMOOTHING_HZ = 1.655f;
// Stereo: the right side's sweep this share of a cycle ahead of the left's.
constexpr double STEREO_LEAD = 0.125;
constexpr std::array<float, 5> POWER{2.45f, 2.45f, 2.45f, 2.55f, 2.62f};

// Rate: the LFO at KNOB_AT (Hz; periods 18.1, 8.1, 3.07, 0.40 and 0.10 s), the manual's
// 0.1–18 s end to end. NAM-X's Rate knob follows this curve, log-interpolated in between.
constexpr std::array<float, 5> RATE_HZ{0.0553f, 0.1228f, 0.326f, 2.52f, 10.0f};

// Res: STANDARD's gain per pass round the loop at 1 kHz (−16.7, −8.1, −3.5 dB; fully clockwise
// 0.89, from guitar with the delay standing still, 2026-11-10: the whole output cancels to
// −31 dB there against −18 at October's 0.849).
constexpr std::array<float, 5> STANDARD_RES{0.0f, 0.146f, 0.394f, 0.668f, 0.890f};
// The longest delay a line holds: the longest sweep, with room.
constexpr float MAX_DELAY_MS = 16.0f + 2.0f;

inline float db_to_gain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

// A knob at `fraction` of its rotation through a map at KNOB_AT.
inline float knob_map(const std::array<float, 5>& map, float fraction) {
    const float x = std::clamp(fraction, 0.0f, 1.0f);
    size_t i = 0;
    while (i + 2 < KNOB_AT.size() && x > KNOB_AT[i + 1]) {
        ++i;
    }
    const float t = (x - KNOB_AT[i]) / (KNOB_AT[i + 1] - KNOB_AT[i]);
    return map[i] + t * (map[i + 1] - map[i]);
}

// The same, interpolating the logarithm (delays).
inline float knob_map_log(const std::array<float, 5>& map, float fraction) {
    std::array<float, 5> logs{};
    std::transform(map.begin(), map.end(), logs.begin(), [](float v) {
        return std::log(v);
    });
    return std::exp(knob_map(logs, fraction));
}

// NAM-X's knobs are plain curves over the pedal's range, not its knobs' positions (the user,
// 2026-11-10; tools/capture/README.md, Knob curves): Manual in ms (log), Depth 0–1 linear in
// the swing (the floor's blend following along the pedal's path), Feedback as STANDARD's gain
// per pass. These convert between the two scales (captures, and settings saved before preset
// version 7).
template <typename F>
inline float position_of(const std::array<float, 5>& map, float value, F scale) {
    const bool rising = map.back() > map.front();
    const float v =
        std::clamp(value, std::min(map.front(), map.back()), std::max(map.front(), map.back()));
    size_t i = 0;
    while (i + 2 < map.size() && (rising ? v > map[i + 1] : v < map[i + 1])) {
        ++i;
    }
    const float t = (scale(v) - scale(map[i])) / (scale(map[i + 1]) - scale(map[i]));
    return KNOB_AT[i] + t * (KNOB_AT[i + 1] - KNOB_AT[i]);
}
inline float manual_ms_from_pedal(float position) {
    return knob_map_log(MANUAL_MS, position);
}
inline float manual_position(float ms) {
    return position_of(MANUAL_MS, ms, [](float v) {
        return std::log(v);
    });
}
inline float depth_from_pedal(float position) {
    return knob_map(SWING_MS, position) / SWING_MS.back();
}
inline float pedal_depth(float depth) {
    return position_of(SWING_MS, depth * SWING_MS.back(), [](float v) {
        return v;
    });
}
inline float feedback_from_pedal(float position) {
    return knob_map(STANDARD_RES, position);
}
inline float res_position(float feedback) {
    return position_of(STANDARD_RES, feedback, [](float v) {
        return v;
    });
}

// The sweep's floor and swing (ms after the dry, the tone's delay taken off) for one setting.
struct Sweep
{
    float floor_ms = 0.0f;
    float swing_ms = 0.0f;
    float power = 2.5f;

    // The pedal's Manual and Depth positions (manual_position(), pedal_depth() of NAM-X's knobs).
    void set(float manual_fraction, float depth_fraction, float tone_delay_ms) {
        const float b = knob_map(BLEND, depth_fraction);
        floor_ms =
            (1.0f - b) * knob_map_log(MANUAL_MS, manual_fraction) + b * LOW_MS - tone_delay_ms;
        swing_ms = knob_map(SWING_MS, depth_fraction);
        power = knob_map(POWER, depth_fraction);
    }
};

// The LFO: a triangle, one-pole smoothed, to the sweep's power. One per processor. The phase
// is a double: a float's steps near 1 are 6e-8, 5 % of the slowest sweep's increment per sample.
struct Lfo
{
    double phase = 0.0;
    float smoothed = 0.0f;

    // The sweep's position (0–1) now, then a step on.
    float next(double phase_step, float smoothing_k, float power) {
        const float triangle = 1.0f - std::abs(2.0f * static_cast<float>(phase) - 1.0f);
        smoothed += smoothing_k * (triangle - smoothed);
        phase += phase_step;
        if (phase >= 1.0) {
            phase -= 1.0;
        }
        return std::pow(std::clamp(smoothed, 0.0f, 1.0f), power);
    }

    void reset() {
        phase = 0.0;
        smoothed = 0.0f;
    }
};

inline float smoothing_coefficient(double sr) {
    return static_cast<float>(1.0 - std::exp(-2.0 * std::numbers::pi * SMOOTHING_HZ / sr));
}

} // namespace bf3

} // namespace namx
