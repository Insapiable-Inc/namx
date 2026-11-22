#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "BiquadChain.h"
#include "Common.h"

namespace namx
{

// The Boss CS-3 Compression Sustainer, fitted to captures of a real one (2026-10-27 to
// 2026-11-11; the study and its data are in captures/boss/cs-3/README.md). Analog: a VCA and a
// peak detector.
//
//   env   = max(|x|, its own release): charges at the attack rate; released, it relaxes in dB
//           toward a fixed floor far below the knee and is caught again by the input on the way:
//             env_dB → floor + (env_dB − floor) · exp(−1/(fs·T))
//   gain  = G − (1 − SLOPE) · max(0, env + G − CEILING)       (dB; a hard knee)
//   out   = TONE( x · gain )
//
// G is Sustain: the gain before the ceiling, here expressed as the input level that reaches the
// ceiling (G = CEILING − threshold, plus a little that follows the release: below), so the
// Compressor's Threshold knob keeps its meaning. The ceiling is the pedal's output at full
// Level; the Compressor's Level (makeup) adds to it. The pedal's Attack knob mostly sets the
// release: the Compressor's Release is T, the time scale of that relaxation, over the knob's
// span; the clamp-down time follows from the same position.
//
// On guitar (the focused session, 2026-11-11) the gain follows the pedal's within 0.2–0.8 dB rms
// while it plays and within 0.1–0.45 dB through the recoveries between hits, at Sustain 7:00,
// 12:00, 5:00 and Attack 7:00, 12:00, 5:00. Until 2026-11-11 this was the "CS-3 (refit)" type
// beside a first CS-3 whose detector released in amplitude (one time constant: recoveries
// 0.5–5 dB off); that one is gone. Known misses: on low notes at a fast release the compressed
// level is ~1 dB low and the distortion higher than the pedal's, pick spikes come out ~2 dB
// small, and at Sustain 7:00 the pedal's detector answers a steady tone 1 dB sooner and a
// chord's peaks later.
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
// ceiling. The Threshold knob is clamped to this span. 5:00 is from the focused session's probe
// (60 dB under the guitar's peak: the gain with nothing played, 2.9 dB over October's reading,
// whose quietest step was compressed already).
constexpr float THRESHOLD_LOW_DB = -49.73f; // Sustain 5:00
constexpr float THRESHOLD_HIGH_DB = -14.26f; // Sustain 7:00

// Knob maps at 7:00, 9:00, 12:00, 3:00, 5:00 (fraction of the rotation), linear in between.
constexpr std::array<float, 5> KNOB_AT{0.0f, 0.2f, 0.5f, 0.8f, 1.0f};

// Attack 7:00 … 5:00. The detector's charge time (ms; calibrated in October by rendering
// against the pedal's −30/0/−30 dB jumps: clamp-down within 2 ms), and T, the time scale of the
// release's dB relaxation (not the 63 % recovery, which depends on the drop): each position's
// from four recoveries at Sustain 5:00 (the session 2026-11-11-attack: exponentials within
// 0.02–0.06 dB, the four within 1 % of each other), log interpolation. The level it relaxes
// toward is fixed, whatever Sustain is: after a chord the gain climbs in dB toward one
// asymptote, 7.7 dB over the most gain Sustain 5:00 gives, and stops where Sustain puts it.
constexpr std::array<float, 5> ATTACK_TAU_MS{1.97f, 1.82f, 1.87f, 1.68f, 1.58f};
constexpr float FLOOR_DB = -57.5f; // re REFERENCE_DBFS
constexpr std::array<float, 5> RELEASE_MS{1152.0f, 870.0f, 300.0f, 142.0f, 99.5f};
// What the Release knob meant until preset version 9: the gain's 63 % recovery after a 30 dB
// drop, at the same positions (release_from_recovery turns a saved value into T).
constexpr std::array<float, 5> RECOVERY_MS{458.0f, 354.0f, 124.0f, 62.0f, 44.0f};
// The most gain there is (the gain with nothing played) rises as the release gets faster: over
// the eight Attack positions of 2026-11-11 a straight line in log T, within 0.04 dB (1.08 dB
// from 7:00 to 5:00). dB per e-fold of T, re Attack 12:00.
constexpr float GAIN_DB_PER_E = 0.442f;

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

// The same, interpolating the logarithm (times).
inline float knob_map_log(const std::array<float, 5>& map, float fraction) {
    std::array<float, 5> logs{};
    std::transform(map.begin(), map.end(), logs.begin(), [](float v) {
        return std::log(v);
    });
    return std::exp(knob_map(logs, fraction));
}

// A position on a table of times (7:00 … 5:00, falling) for a time: log interpolation,
// clamped to the span.
inline float time_fraction(const std::array<float, 5>& table, float ms) {
    const float x = std::log(std::clamp(ms, table.back(), table.front()));
    for (size_t i = 0; i + 1 < table.size(); ++i) {
        const float a = std::log(table[i]), b = std::log(table[i + 1]);
        if (x >= b) {
            return KNOB_AT[i] + (a - x) / (a - b) * (KNOB_AT[i + 1] - KNOB_AT[i]);
        }
    }
    return 1.0f;
}

// The Attack knob's position for a Release (T, ms).
inline float attack_fraction(float release_ms) {
    return time_fraction(RELEASE_MS, release_ms);
}

// The Release (T, ms) at the Attack position an older preset's recovery time stood for.
inline float release_from_recovery(float recovery_ms) {
    return knob_map_log(RELEASE_MS, time_fraction(RECOVERY_MS, recovery_ms));
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
