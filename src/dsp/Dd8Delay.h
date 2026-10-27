#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "BiquadChain.h"
#include "Common.h"

namespace namx
{

// The Boss DD-8 Digital Delay's Standard, Analog, Tape, Warm and Mod modes, as voicings
// of one engine, from captures of a real one (2026-10-26/27; the study and its data are
// in captures/boss/dd-8/README.md — "The four voicings side by side" is this file's
// table). Per line:
//
//   r    = loop chain( line read at the delay time (± the voicing's LFO) )
//   wet  = Level · out chain(r)                    Level: the DD-8's taper + repeat gain
//   line ← clip( input + Feedback · loop low-pass(r) )
//   out  = dry + wet                               the dry path is untouched (analog)
//
// The loop chain acts on every pass (Analog: a steep 2.8 kHz low-pass, each repeat
// darker); the out chain only shapes what is heard (Standard's 10 kHz wall, Tape's band,
// Warm's tilt); the loop low-pass is the small extra high cut per pass measured in
// Standard. Only Standard's and Analog's filter placement was measured: Tape's band and
// Warm's tilt sit on the output, where a boost can't build up over the repeats.
//
// The clip is before the line, as in the pedal (before Level, after the feedback sum):
// fitted to the repeats' gain loss against the input level (0.1–0.2 dB); a static curve
// can't also match the pedal's third harmonic, which it overstates by 3–6 dB. Its
// ceiling is relative to REFERENCE_DBFS, which stands for the hard guitar peak the pedal
// was measured against. Mono and allocation-free; the caller keeps the filter state.
namespace dd8
{

// A hard-played guitar's peak after a loudness-normalized amp model (-18 dB): where
// the pedal's clip is calibrated from. An assumption — the pedal sat before the amp.
constexpr float REFERENCE_DBFS = -6.0f;

// The positions the knob maps are measured at: 9:00, 12:00, 3:00, 5:00 (fraction of the
// rotation). At 7:00 both Level and Feedback are off.
constexpr std::array<float, 4> KNOB_AT{0.2f, 0.5f, 0.8f, 1.0f};

// Level, dB re 5:00 (an audio taper; the same in every mode within ~1 dB).
constexpr std::array<float, 4> LEVEL_DB{-33.6f, -13.4f, -2.6f, 0.0f};

constexpr size_t CHAIN = 4;
using Chain = FilterChain<CHAIN>;

struct Voicing
{
    float repeat_db = 0.0f; // the first repeat over the dry at Level 5:00
    float ceiling_db = 0.0f; // the clip, re REFERENCE_DBFS
    float knee = 0.0f; // 0: hard; else the soft clip's exponent n (x / (1 + |x/c|^n)^(1/n))
    std::array<float, 4> feedback_db{}; // per repeat at 1 kHz, at KNOB_AT
    Chain loop{}; // on every pass
    Chain out{}; // on the output only
    float loop_low_pass_hz = 0.0f; // first-order, on the feedback path (0: none)
    float lfo_hz = 0.0f; // sine on the delay time (Mod; Tape's wow)
    float lfo_ms = 0.0f; // its depth, ± ms
    float flutter_hz = 0.0f; // a second sine (Tape)
    float flutter_ms = 0.0f;
};

// A 6th-order Butterworth low-pass at 2.78 kHz: flat to 2 kHz, −19 dB at 4 kHz, −55 dB at
// 8 kHz (measured −19 / −50). No peak above its 1 kHz gain, so it can sit in the loop
// without a band running away at high Feedback.
constexpr Chain ANALOG_LOOP{{
    {FilterKind::LOW_PASS, 2780.0f, 0.5176f, 0.0f},
    {FilterKind::LOW_PASS, 2780.0f, 0.7071f, 0.0f},
    {FilterKind::LOW_PASS, 2780.0f, 1.9319f, 0.0f},
    {},
}};

// The first repeat's tone, re 1 kHz: flat to 4 kHz, −2.1 dB at 8 kHz, −29.5 dB at 16 kHz,
// +1.5 dB at 63 Hz (fitted within 0.2 dB per third octave).
constexpr Chain STANDARD_OUT{{
    {FilterKind::LOW_PASS, 6248.9f, 0.721f, 0.0f},
    {FilterKind::LOW_PASS, 11118.6f, 2.5f, 0.0f},
    {FilterKind::PEAK, 60.3f, 0.917f, 1.61f},
    {},
}};

constexpr float LOOP_LOW_PASS_HZ = 13000.0f; // −1.4 dB at 8 kHz per pass

constexpr Voicing STANDARD{
    .repeat_db = 4.46f,
    .ceiling_db = -4.10f,
    .feedback_db = {-12.23f, -7.07f, -0.6f, 0.02f},
    .out = STANDARD_OUT,
    .loop_low_pass_hz = LOOP_LOW_PASS_HZ,
};

// Darker on every pass, repeats 1.6 dB hotter, a soft knee; Feedback 5:00 runs away
// slowly (+0.4 dB per repeat), held by the clip.
constexpr Voicing ANALOG{
    .repeat_db = 6.03f,
    .ceiling_db = -6.12f,
    .knee = 26.9f,
    .feedback_db = {-12.0f, -7.9f, -1.40f, 0.4f},
    .loop = ANALOG_LOOP,
};

// A ~150 Hz – 5 kHz band (−15 / −9.6 dB at 31.5 / 63 Hz, +1.2 dB at 250 Hz, −13.9 dB at
// 8 kHz, −37 dB at 16 kHz; within 1 dB), wow (0.53 Hz, 2.0 cents rms) and flutter
// (6.7 Hz, 0.6 cents rms) — as delay swings, 2.0 cents rms at 0.53 Hz is ±0.49 ms. The
// measured 2.4 dB level wobble is left out (its phase against the wow is unknown).
constexpr Voicing TAPE{
    .repeat_db = 7.9f,
    .ceiling_db = -7.75f,
    .feedback_db = {-10.8f, -6.65f, -0.04f, 0.003f},
    .out = {{
        {FilterKind::HIGH_PASS, 62.5f, 0.4f, 0.0f},
        {FilterKind::LOW_PASS, 12809.8f, 1.437f, 0.0f},
        {FilterKind::LOW_PASS, 3510.1f, 1.023f, 0.0f},
        {FilterKind::PEAK, 320.5f, 0.867f, 3.57f},
    }},
    .loop_low_pass_hz = LOOP_LOW_PASS_HZ,
    .lfo_hz = 0.53f,
    .lfo_ms = 0.49f,
    .flutter_hz = 6.7f,
    .flutter_ms = 0.0116f,
};

// Standard with a warm tilt: +2–3 dB below 250 Hz, −1.6 dB at 4 kHz, −4.7 dB at 8 kHz,
// −18 dB at 16 kHz (within 0.6 dB), 1.6 dB quieter, the clip 1.6 dB later.
constexpr Voicing WARM{
    .repeat_db = 2.85f,
    .ceiling_db = -2.46f,
    .feedback_db = {-11.6f, -7.46f, -0.82f, 0.06f},
    .out = {{
        {FilterKind::LOW_SHELF, 504.3f, 0.429f, 3.45f},
        {FilterKind::LOW_PASS, 7168.2f, 0.598f, 0.0f},
        {FilterKind::LOW_PASS, 13778.6f, 2.5f, 0.0f},
        {},
    }},
    .loop_low_pass_hz = LOOP_LOW_PASS_HZ,
};

// Standard plus a 1.51 Hz sine of ±0.62 ms on the delay (the same at every Time). Its
// tone couldn't be measured (the LFO smears the sweeps): Standard's, as everything else
// matches Standard.
constexpr Voicing MOD{
    .repeat_db = 4.47f,
    .ceiling_db = -4.10f,
    .feedback_db = {-12.2f, -7.45f, -0.84f, 0.02f},
    .out = STANDARD_OUT,
    .loop_low_pass_hz = LOOP_LOW_PASS_HZ,
    .lfo_hz = 1.51f,
    .lfo_ms = 0.62f,
};

inline float db_to_gain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

// A knob at `fraction` of its rotation through a map in dB at KNOB_AT, linear in dB in
// between; below 9:00 the gain falls linearly to 0 at 7:00.
inline float knob_gain(const std::array<float, 4>& db, float fraction) {
    const float x = std::clamp(fraction, 0.0f, 1.0f);
    if (x < KNOB_AT[0]) {
        return x / KNOB_AT[0] * db_to_gain(db[0]);
    }
    size_t i = 0;
    while (i + 2 < KNOB_AT.size() && x > KNOB_AT[i + 1]) {
        ++i;
    }
    const float t = (x - KNOB_AT[i]) / (KNOB_AT[i + 1] - KNOB_AT[i]);
    return db_to_gain(db[i] + t * (db[i + 1] - db[i]));
}

// The clip, with the ceiling as a linear amplitude.
inline float clip(float x, float ceiling, float knee) {
    if (knee <= 0.0f) {
        return std::clamp(x, -ceiling, ceiling);
    }
    const float u = std::abs(x) / ceiling;
    if (u < 0.5f) {
        return x; // (1/2)^n is below float precision for the fitted knees
    }
    return x / std::pow(1.0f + std::pow(u, knee), 1.0f / knee);
}

// A voicing's filters at one sample rate.
struct Coefficients
{
    BiquadChain<CHAIN> loop;
    BiquadChain<CHAIN> out;
    float loop_low_pass = 1.0f; // one-pole coefficient (1: pass-through)

    void prepare(const Voicing& v, double sr) {
        loop.design(v.loop, sr);
        out.design(v.out, sr);
        loop_low_pass = v.loop_low_pass_hz > 0.0f
                            ? static_cast<float>(
                                  1.0 - std::exp(-2.0 * std::numbers::pi * v.loop_low_pass_hz / sr))
                            : 1.0f;
    }
};

} // namespace dd8

} // namespace namx
