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
// (2026-10-27; the study and its data are in captures/boss/bf-3/README.md,
// tools/capture/analyze_bf3.py measures STANDARD). A digital flanger:
//
//   STANDARD  line = x + r(Res) · y               y = TONE( line(t − d(t)) )
//             out  = clip( DRY · x + WET · y )
//   ULTRA     v    = x + ρ(Res) · TONE( v(t − d) )            the loop re-enters before
//             out  = clip( DRY · v + TONE( Σ G_k · c_k(Res) · v(t − k·d/2) ) ), k = 1..3
//   d(t)  = floor + swing(Depth) · s(t)^2.5       (after the dry)
//   floor = (1 − b(Depth)) · m(Manual) + b(Depth) · 0.33 ms
//   s(t)  = a 0–1 triangle at the Rate, through a one-pole low-pass at 1.53 Hz
//
// ULTRA is four evenly spaced copies — the dry and three taps at d/2, d, 3d/2, nearly
// equal — with STANDARD's sweep on the middle one. Its Res wiring is the study's best
// reading, not proven; its echoes' levels are calibrated to the pedal's (ULTRA_RES).
// The pedal delays its dry by 0.55 ms (converter latency); here the dry is undelayed and
// the delays are the pedal's dry-to-wet spacings.
namespace bf3
{

// A hard-played guitar's peak, as for the DD-8 and CE-5 (Dd8Delay.h, Ce5Chorus.h): an
// assumption — the clip sits 0.55 dB over the guitar's peak in the study's rig.
constexpr float REFERENCE_DBFS = -6.0f;
constexpr float CEILING_DB = 0.55f; // re REFERENCE_DBFS, on the summed output

// TONE's own delay (phase slope at 300 Hz–5 kHz), part of every measured spacing: taken
// off the delays so the dry-to-wet spacing stays the pedal's.
constexpr float TONE_DELAY_MS = 0.068f;

// The wet's tone, re 1 kHz (within 0.33 dB rms, 90 Hz–18 kHz); also each pass round the loop.
constexpr size_t TONE_SECTIONS = 4;
constexpr FilterChain<TONE_SECTIONS> TONE{{
    {FilterKind::HIGH_PASS, 191.8f, 0.519f, 0.0f},
    {FilterKind::HIGH_PASS, 94.0f, 1.556f, 0.0f},
    {FilterKind::LOW_PASS, 6293.0f, 0.542f, 0.0f},
    {FilterKind::LOW_PASS, 17471.7f, 0.923f, 0.0f},
}};

// Levels at 1 kHz re bypass.
constexpr float STANDARD_DRY_DB = 1.79f;
constexpr float STANDARD_WET_DB = 3.41f;
constexpr float ULTRA_DRY_DB = 1.03f;
constexpr std::array<float, 3> ULTRA_TAP_DB{0.76f, 0.64f, 0.41f}; // at d/2, d, 3d/2

// Knob maps at 7:00, 9:00, 12:00, 3:00, 5:00 (fraction of the rotation), linear in between.
constexpr std::array<float, 5> KNOB_AT{0.0f, 0.2f, 0.5f, 0.8f, 1.0f};

// Manual: the delay after the dry at Depth minimum (ms; log interpolation).
constexpr std::array<float, 5> MANUAL_MS{14.673f, 9.065f, 2.005f, 0.701f, 0.447f};

// Depth: how far the floor moves from Manual's delay toward LOW_MS, and the swing (as at a
// slow Rate; the smoothing takes some off as Rate rises).
constexpr float LOW_MS = 0.33f;
constexpr std::array<float, 5> BLEND{0.0f, 0.368f, 0.657f, 0.882f, 0.984f};
constexpr std::array<float, 5> SWING_MS{0.0f, 1.102f, 4.867f, 10.655f, 14.582f};

constexpr float SMOOTHING_HZ = 1.53f;
constexpr float POWER = 2.5f;

// Res: STANDARD's gain per pass round the loop at 1 kHz (−16.7, −8.1, −3.5, −1.4 dB).
constexpr std::array<float, 5> STANDARD_RES{0.0f, 0.146f, 0.394f, 0.668f, 0.849f};
// ULTRA: the loop's gain (re the dry) and the second and third taps' levels. The taps'
// trims are the structure fit's; the gains are calibrated by rendering this model against
// the pedal's echo levels at 1 kHz (the first eight d/2 slots, Manual 7:00: within 1.9,
// 1.4, 0.9, 0.6 dB rms for Res 9:00 … 5:00).
constexpr std::array<float, 5> ULTRA_RES{0.0f, 0.153f, 0.393f, 0.678f, 0.894f};
constexpr std::array<float, 5> ULTRA_TAP2{1.0f, 0.91f, 0.80f, 0.74f, 0.76f};
constexpr std::array<float, 5> ULTRA_TAP3{1.0f, 0.85f, 0.66f, 0.57f, 0.61f};

// The longest delay any tap reads: 3/2 of the longest sweep, with room.
constexpr float MAX_DELAY_MS = 1.5f * 16.0f + 2.0f;

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

// The sweep's floor and swing (ms after the dry, TONE's delay taken off) for one setting.
struct Sweep
{
    float floor_ms = 0.0f;
    float swing_ms = 0.0f;

    void set(float manual_fraction, float depth_fraction) {
        const float b = knob_map(BLEND, depth_fraction);
        floor_ms =
            (1.0f - b) * knob_map_log(MANUAL_MS, manual_fraction) + b * LOW_MS - TONE_DELAY_MS;
        swing_ms = knob_map(SWING_MS, depth_fraction);
    }
};

// The LFO: a triangle, one-pole smoothed, to POWER. One per processor.
struct Lfo
{
    float phase = 0.0f;
    float smoothed = 0.0f;

    // The sweep's position (0–1) now, then a step on.
    float next(float phase_step, float smoothing_k) {
        const float triangle = 1.0f - std::abs(2.0f * phase - 1.0f);
        smoothed += smoothing_k * (triangle - smoothed);
        phase += phase_step;
        if (phase >= 1.0f) {
            phase -= 1.0f;
        }
        return std::pow(std::clamp(smoothed, 0.0f, 1.0f), POWER);
    }

    void reset() {
        phase = 0.0f;
        smoothed = 0.0f;
    }
};

inline float smoothing_coefficient(double sr) {
    return static_cast<float>(1.0 - std::exp(-2.0 * std::numbers::pi * SMOOTHING_HZ / sr));
}

} // namespace bf3

} // namespace namx
