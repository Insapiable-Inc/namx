#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "BiquadChain.h"
#include "Common.h"

namespace namx
{

// The Boss CE-5 Chorus Ensemble, fitted to captures of a real one (2026-10-27; the study
// and its data are in captures/boss/ce-5/README.md, tools/capture/analyze_ce5.py
// measures them). A digital chorus:
//
//   x'   = clip(x)                                  the input converter, dry and wet
//   dry  = DRY_GAIN · x'                            +0.35 dB over bypass
//   wet  = High · Low · TONE( clip_wet( E(E.Level) · x'(t − d(t)) ) )
//   d(t) = centre − swing/2 + swing · u^power       u: a 0–1 triangle, 1 at the top
//   mono out = dry + wet;  stereo out = wet | dry   (the pedal's Output A | Output B)
//
// The delay's centre, swing and bend come from Depth (measured at Rate 12:00); the swing
// shrinks as Rate goes up (by a Depth-dependent 13–28 %; one mean curve here). Rate itself
// stays in Hz, as the Chorus's knob. The pedal delays its dry path by 0.98 ms (converter
// latency); here the dry is undelayed and the wet's delays are taken relative to it.
// TONE is the mono output's wet tone (the wet-only output is ~2 dB brighter at 6–12 kHz).
// Low (outer knob) is a resonant low shelf, High (inner) a first-order low-pass; both
// on the chorus sound only, fitted per knob position.
namespace ce5
{

// A hard-played guitar's peak after a loudness-normalized amp model, as for the DD-8
// (Dd8Delay.h): an assumption, the pedal sat before the amp.
constexpr float REFERENCE_DBFS = -6.0f;

constexpr float DRY_GAIN_DB = 0.35f;
constexpr float DRY_LATENCY_MS = 0.98f;
// TONE's own group delay at 1–5 kHz (the pedal's wall is linear-phase, its delay part of
// the measured ones): taken off the wet's delays so the dry-to-wet spacing stays the pedal's.
constexpr float TONE_DELAY_MS = 0.07f;
constexpr float INPUT_CEILING_DB = 1.18f; // re REFERENCE_DBFS
// After E.Level: 1.02 dB under the wet's level at E.Level 5:00 with the reference in.
constexpr float WET_CEILING_DB = 2.70f - 1.02f;

// Knob maps at 7:00, 9:00, 12:00, 3:00, 5:00 (fraction of the rotation), linear in between.
constexpr std::array<float, 5> KNOB_AT{0.0f, 0.2f, 0.5f, 0.8f, 1.0f};

// E.Level: the wet at 1 kHz re the input (mono output); 7:00 doesn't mute it. (Re-measured
// 2026-10-29: the dry and these were ~2 dB high, the bypass reference read low.)
constexpr std::array<float, 5> ELEVEL_DB{-20.85f, -16.80f, -10.47f, -2.67f, 2.70f};

// Depth, at Rate 12:00 (0.86 Hz): the delay's minimum and swing (ms, re the input) and
// the triangle's bend.
constexpr std::array<float, 5> DELAY_MIN_MS{6.368f, 6.078f, 5.835f, 5.737f, 5.713f};
constexpr std::array<float, 5> SWING_MS{0.033f, 0.621f, 1.44f, 2.133f, 2.422f};
constexpr std::array<float, 5> POWER{1.0f, 1.07f, 1.17f, 1.25f, 1.27f};

// The swing against the LFO rate, re 0.86 Hz (log-Hz interpolation, flat outside).
constexpr std::array<float, 5> SWING_RATE_HZ{0.25f, 0.375f, 0.86f, 2.515f, 5.93f};
constexpr std::array<float, 5> SWING_SCALE{1.135f, 1.09f, 1.0f, 0.915f, 0.867f};

// The wet's tone, re 1 kHz: a ~60 Hz low cut with a small bump above it, flat, and a
// steep wall above 10 kHz (−2.7 dB at 8 kHz, −11 at 12, −25 at 14, −40 at 16; within
// 0.7 dB). The pedal's wall is linear-phase; this one isn't (a chorus doesn't care).
constexpr size_t TONE_SECTIONS = 7;
constexpr FilterChain<TONE_SECTIONS> TONE{{
    {FilterKind::HIGH_PASS, 133.2f, 0.714f, 0.0f},
    {FilterKind::PEAK, 75.7f, 0.879f, 8.0f},
    {FilterKind::PEAK, 12000.0f, 0.812f, -8.0f},
    {FilterKind::LOW_PASS, 16000.0f, 0.4f, 0.0f},
    {FilterKind::LOW_PASS, 16000.0f, 0.4f, 0.0f},
    {FilterKind::LOW_PASS, 9642.9f, 1.12f, 0.0f},
    {FilterKind::LOW_PASS, 12021.1f, 2.563f, 0.0f},
}};

// Low: a low shelf (Q 1.12 at every position: the floor deepens and the corner rises as
// the knob turns down; +1–2 dB of resonance above it; within 0.2–0.4 dB rms). Fully
// clockwise it's flat; the last quarter (9:00 → 7:00) barely changes it.
constexpr float LOW_Q = 1.12f;
constexpr std::array<float, 5> LOW_HZ{314.5f, 298.0f, 188.7f, 102.9f, 102.9f};
constexpr std::array<float, 5> LOW_GAIN_DB{-40.9f, -40.7f, -26.6f, -6.0f, 0.0f};

// High: a first-order low-pass and a small broadband drop (within 0.2–0.3 dB rms).
// Fully clockwise it's flat; between 3:00 and 5:00 the corner rises to 24 kHz.
constexpr std::array<float, 5> HIGH_HZ{639.2f, 1241.1f, 3418.8f, 9463.4f, 24000.0f};
constexpr std::array<float, 5> HIGH_GAIN_DB{-1.78f, -0.90f, -0.17f, 0.01f, 0.0f};

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

// The same, interpolating the logarithm (frequencies).
inline float knob_map_log(const std::array<float, 5>& map, float fraction) {
    std::array<float, 5> logs{};
    std::transform(map.begin(), map.end(), logs.begin(), [](float v) {
        return std::log(v);
    });
    return std::exp(knob_map(logs, fraction));
}

inline float swing_scale(float rate_hz) {
    const float x = std::log(std::max(rate_hz, 1e-3f));
    if (x <= std::log(SWING_RATE_HZ.front())) {
        return SWING_SCALE.front();
    }
    for (size_t i = 0; i + 1 < SWING_RATE_HZ.size(); ++i) {
        const float a = std::log(SWING_RATE_HZ[i]), b = std::log(SWING_RATE_HZ[i + 1]);
        if (x <= b) {
            return SWING_SCALE[i] + (x - a) / (b - a) * (SWING_SCALE[i + 1] - SWING_SCALE[i]);
        }
    }
    return SWING_SCALE.back();
}

// The delay (ms, re the dry) at LFO phase `phase` for one Depth / Rate.
struct Sweep
{
    float low_ms = 0.0f; // the bottom of the swing
    float swing_ms = 0.0f;
    float power = 1.0f;

    void set(float depth_fraction, float rate_hz) {
        const float swing = knob_map(SWING_MS, depth_fraction);
        const float centre = knob_map(DELAY_MIN_MS, depth_fraction) + swing / 2.0f;
        swing_ms = swing * swing_scale(rate_hz);
        low_ms = centre - swing_ms / 2.0f - DRY_LATENCY_MS - TONE_DELAY_MS;
        power = knob_map(POWER, depth_fraction);
    }

    float delay_ms(float phase) const {
        const float u = 1.0f - 2.0f * std::min(phase, 1.0f - phase); // 1 at 0, 0 at 0.5
        return low_ms + swing_ms * std::pow(u, power);
    }
};

// The Low and High knobs' filters at one sample rate.
struct Tone
{
    BiquadChain<TONE_SECTIONS> tone;
    BiquadChain<1> low;
    float high_k = 1.0f; // one-pole coefficient (1: flat)
    float high_gain = 1.0f;
    float low_fraction = -1.0f, high_fraction = -1.0f; // what `low` / `high_k` are set for

    void prepare(double sr) {
        tone.design(TONE, sr);
        low_fraction = high_fraction = -1.0f;
    }

    // Redesigns only when a knob moved (audio thread, allocation-free).
    void set_knobs(float low_knob, float high_knob, double sr) {
        if (low_knob != low_fraction) {
            low_fraction = low_knob;
            const FilterSection shelf{FilterKind::LOW_SHELF, knob_map_log(LOW_HZ, low_knob), LOW_Q,
                                      knob_map(LOW_GAIN_DB, low_knob)};
            // Unity far above the shelf, so its floor and resonance stay where measured.
            low.design({shelf}, sr, 16000.0);
        }
        if (high_knob != high_fraction) {
            high_fraction = high_knob;
            high_k =
                high_knob >= 1.0f
                    ? 1.0f
                    : static_cast<float>(1.0 - std::exp(-2.0 * std::numbers::pi *
                                                        knob_map_log(HIGH_HZ, high_knob) / sr));
            high_gain = db_to_gain(knob_map(HIGH_GAIN_DB, high_knob));
        }
    }
};

// One channel's filter state.
struct ToneState
{
    BiquadChainState<TONE_SECTIONS> tone;
    BiquadChainState<1> low;
    float high = 0.0f;

    float process(float x, const Tone& k) {
        const float shaped = low.process(tone.process(x, k.tone), k.low);
        high += k.high_k * (shaped - high);
        return k.high_gain * high;
    }

    void reset() {
        tone.reset();
        low.reset();
        high = 0.0f;
    }
};

} // namespace ce5

} // namespace namx
