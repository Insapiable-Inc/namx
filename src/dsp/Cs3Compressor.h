#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "BiquadChain.h"
#include "Common.h"

namespace namx
{

// The Boss CS-3 Compression Sustainer, fitted to captures of a real one (2026-10-27; the
// study and its data are in captures/boss/cs-3/README.md, tools/capture/analyze_cs3.py
// measures them). Analog: a VCA and a peak detector.
//
//   env   = peak detector on |x| (charges at the attack rate, discharges at the release)
//   gain  = G − (1 − SLOPE) · max(0, env + G − CEILING)       (dB; a hard knee)
//   out   = TONE( x · gain )
//
// G is Sustain: the gain before the ceiling, here expressed as the input level that
// reaches the ceiling (G = CEILING − threshold), so the Compressor's Threshold knob keeps
// its meaning. The ceiling is the pedal's output at full Level; the Compressor's Level
// (makeup) adds to it. The pedal's Attack knob mostly sets the release, so it's read from
// the Compressor's Release (the gain's 63 % recovery after a loud part); its clamp-down
// time follows from the same knob position.
namespace cs3
{

// A hard-played guitar's peak, as for the other pedal models (Dd8Delay.h, Ce5Chorus.h,
// Bf3Flanger.h): an assumption — the pedal sat before the amp.
constexpr float REFERENCE_DBFS = -6.0f;

// The output's ceiling at full Level (a sine's peak, re REFERENCE_DBFS) and the slope
// above it (dB out per dB in; ~85:1). The knee is hard (fitted width 0.02 dB).
constexpr float CEILING_DB = -14.25f;
constexpr float SLOPE = 0.0117f;

// Sustain 7:00 … 5:00 as thresholds (re REFERENCE_DBFS): the input level that reaches the
// ceiling. The Threshold knob is clamped to this span.
constexpr float THRESHOLD_LOW_DB = -46.83f; // Sustain 5:00
constexpr float THRESHOLD_HIGH_DB = -14.26f; // Sustain 7:00

// Knob maps at 7:00, 9:00, 12:00, 3:00, 5:00 (fraction of the rotation), linear in between.
constexpr std::array<float, 5> KNOB_AT{0.0f, 0.2f, 0.5f, 0.8f, 1.0f};

// Attack: the gain's 63 % recovery after a 30 dB drop (ms, as measured — the Release
// knob's scale), and the detector's time constants that reproduce it (ms), calibrated by
// rendering this model against the pedal's −30/0/−30 dB jumps (gain envelopes within
// 0.27–0.34 dB rms; recovery within 6 ms, clamp-down within 2 ms).
constexpr std::array<float, 5> RECOVERY_MS{458.0f, 354.0f, 124.0f, 62.0f, 44.0f};
constexpr std::array<float, 5> ATTACK_TAU_MS{1.97f, 1.82f, 1.87f, 1.68f, 1.58f};
constexpr std::array<float, 5> RELEASE_TAU_MS{189.1f, 145.9f, 49.6f, 24.3f, 16.4f};

// Tone (after the VCA, by the schematic): a 38 Hz low cut and an 18 kHz roll-off, fixed, and two low shelves (656 Hz,
// 5.5 kHz) whose gains follow the knob — a tilt around 1 kHz (within 0.2–0.3 dB rms; refitted
// 2026-10-29 with tools/capture/fit_voicings.py to the corrected sweeps).
constexpr size_t TONE_SECTIONS = 4;
constexpr float SHELF_LOW_HZ = 655.8f, SHELF_LOW_Q = 0.480f;
constexpr float SHELF_HIGH_HZ = 5456.4f, SHELF_HIGH_Q = 0.661f;
constexpr std::array<float, 5> SHELF_LOW_DB{9.85f, 6.63f, 0.57f, -5.41f, -9.21f};
constexpr std::array<float, 5> SHELF_HIGH_DB{2.67f, 1.29f, 1.95f, 2.81f, 1.43f};
// The chain is unity at 1 kHz; the knob also moves the level there (re 12:00): the tilt
// pivots near 100–200 Hz, Tone mostly adds or takes away treble.
constexpr std::array<float, 5> TONE_GAIN_DB{-6.1f, -4.3f, 0.0f, 4.4f, 6.7f};

inline float knob_map(const std::array<float, 5>& map, float fraction) {
    const float x = std::clamp(fraction, 0.0f, 1.0f);
    size_t i = 0;
    while (i + 2 < KNOB_AT.size() && x > KNOB_AT[i + 1]) {
        ++i;
    }
    const float t = (x - KNOB_AT[i]) / (KNOB_AT[i + 1] - KNOB_AT[i]);
    return map[i] + t * (map[i + 1] - map[i]);
}

// The Attack knob's position for a recovery time (log interpolation, clamped to the span).
inline float attack_fraction(float recovery_ms) {
    const float x = std::log(std::clamp(recovery_ms, RECOVERY_MS.back(), RECOVERY_MS.front()));
    for (size_t i = 0; i + 1 < RECOVERY_MS.size(); ++i) {
        const float a = std::log(RECOVERY_MS[i]), b = std::log(RECOVERY_MS[i + 1]);
        if (x >= b) {
            return KNOB_AT[i] + (a - x) / (a - b) * (KNOB_AT[i + 1] - KNOB_AT[i]);
        }
    }
    return 1.0f;
}

inline FilterChain<TONE_SECTIONS> tone_chain(float fraction) {
    return {{
        {FilterKind::HIGH_PASS, 38.3f, 0.467f, 0.0f},
        {FilterKind::LOW_PASS, 18000.0f, 0.482f, 0.0f},
        {FilterKind::LOW_SHELF, SHELF_LOW_HZ, SHELF_LOW_Q, knob_map(SHELF_LOW_DB, fraction)},
        {FilterKind::LOW_SHELF, SHELF_HIGH_HZ, SHELF_HIGH_Q, knob_map(SHELF_HIGH_DB, fraction)},
    }};
}

} // namespace cs3

} // namespace namx
