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
//   wet  = LOW_CUT( clip_wet( High · Low · TOP( E(E.Level) · x'(t − d(t)) ) ) )
//   d(t) = low + swing · (e^(bend·v) − 1) / (e^bend − 1)    v: a 0–1 triangle, its sides rounded a little
//   mono out = dry + wet   (the pedal's Output A alone)
//   stereo out = dry + wet | dry + wet', wet' on a sweep half a cycle behind: ours (the user,
//   2026-11-10), each side alone the mono pedal. The pedal's own stereo is wet | dry (its
//   Output A | Output B), a chorus only once two speakers mix in the room.
//
// The delay's minimum, swing and bend come from Depth (measured at Rate 12:00); Rate scales
// the swing and the bend and shifts the minimum (two measured curves, at Depth 12:00 and
// 5:00). Rate itself stays in Hz, as the Chorus's knob. The pedal delays its dry path by
// 0.98 ms (converter latency); here the dry is undelayed and the wet's delays are taken
// relative to it. The tone is the chorus-alone output's (OUTPUT A with a plug in B); a probe
// tone reads the same on the mono output.
// Low (outer knob) is a resonant low shelf, High (inner) a first-order low-pass; both
// on the chorus sound only, fitted per knob position.
namespace ce5
{

// A hard-played guitar's peak after a loudness-normalized amp model, as for the DD-8
// (Dd8Delay.h): an assumption, the pedal sat before the amp.
constexpr float REFERENCE_DBFS = -6.0f;

constexpr float DRY_GAIN_DB = 0.35f;
constexpr float DRY_LATENCY_MS = 0.98f;
// The tone filters' own group delay at 1–5 kHz (the pedal's wall is linear-phase, its delay part of
// the measured ones): taken off the wet's delays so the dry-to-wet spacing stays the pedal's.
constexpr float TONE_DELAY_MS = 0.039f;
constexpr float INPUT_CEILING_DB = 1.18f; // re REFERENCE_DBFS
// After E.Level: 1.02 dB under the wet's level at E.Level 5:00 with the reference in.
constexpr float WET_CEILING_DB = 2.70f - 1.02f;

// Knob maps at 7:00, 9:00, 12:00, 3:00, 5:00 (fraction of the rotation), linear in between.
constexpr std::array<float, 5> KNOB_AT{0.0f, 0.2f, 0.5f, 0.8f, 1.0f};

// E.Level: the wet at 1 kHz re the input (mono output); 7:00 doesn't mute it. (Re-measured
// 2026-10-29: the dry and these were ~2 dB high, the bypass reference read low.)
constexpr std::array<float, 5> ELEVEL_DB{-20.85f, -16.80f, -10.47f, -2.67f, 2.70f};

// The sweep, read from a probe tone under the guitar (2026-11-10) and October's held tones:
//
//   u     a triangle: 1 at phase 0, down to 0 at `duty`, back up to 1
//   v     = (1 − ROUND) · u + ROUND · (1 − cos πu) / 2         each side rounded a little
//   delay = low + swing · (e^(bend·v) − 1) / (e^bend − 1)
//
// A corner at the bottom as at the top (the delay's speed — the pitch — jumps at both), the
// sides bowed. Within 3–7 µs of the pedal at every Rate and Depth captured.
constexpr float ROUND = 0.25f;

// Depth, at Rate 12:00 (0.86 Hz): the delay's minimum and swing (ms, re the input) and the bend.
constexpr std::array<float, 5> DELAY_MIN_MS{6.359f, 6.076f, 5.834f, 5.724f, 5.692f};
constexpr std::array<float, 5> SWING_MS{0.032f, 0.597f, 1.399f, 2.089f, 2.377f};
constexpr std::array<float, 5> BEND{0.0f, 0.153f, 0.425f, 0.564f, 0.606f};

// Rate: the LFO at KNOB_AT (Hz). NAM-X's Rate knob follows this curve, log-interpolated in
// between.
constexpr std::array<float, 5> RATE_HZ{0.250f, 0.375f, 0.858f, 2.515f, 5.934f};

// What the rate does to the sweep, re 0.86 Hz, at RATE_HZ (log-Hz interpolation, flat outside):
// the swing and the bend scale, the minimum shifts (ms), the triangle's falling share changes.
// Measured at Depth 12:00 (used from there down) and at 5:00 (its 9:00 and 3:00 entries placed
// as the 12:00 curve's are); Depth blends the two in between.
constexpr std::array<float, 5> SWING_SCALE_MID{1.180f, 1.109f, 1.0f, 0.929f, 0.904f};
constexpr std::array<float, 5> SWING_SCALE_FULL{1.102f, 1.062f, 1.0f, 0.954f, 0.937f};
constexpr std::array<float, 5> BEND_SCALE_MID{1.329f, 1.195f, 1.0f, 0.861f, 0.816f};
constexpr std::array<float, 5> BEND_SCALE_FULL{1.472f, 1.280f, 1.0f, 0.841f, 0.790f};
constexpr std::array<float, 5> MIN_SHIFT_MS_MID{-0.117f, -0.072f, 0.0f, 0.070f, 0.066f};
constexpr std::array<float, 5> MIN_SHIFT_MS_FULL{-0.231f, -0.142f, 0.0f, 0.144f, 0.144f};
constexpr std::array<float, 5> DUTY{0.500f, 0.513f, 0.5295f, 0.539f, 0.542f};

// The wet's tone, re 1 kHz, refitted 2026-11-10 to the chorus-alone output (OUTPUT A with a
// plug in B) with the delay standing still (tools/capture/ce5/tone.py). Two parts, the wet
// clip between them (the burst ladders: the clip comes after the Low and High filters, and a
// 200 Hz tone leaves it 1 dB louder than a 1 kHz one):
//  - the top, before the clip: flat to 8 kHz, then the wall (−4 dB at 11 kHz, −14 at 12.5,
//    −24 at 14; within 0.3 dB to 11 kHz, the delay line's own loss between samples counted
//    in). The pedal's wall is linear-phase; this one isn't (a chorus doesn't care).
//  - the low cut, after it: a fourth-order high-pass (−6 dB at 63 Hz, −17 at 40) with a
//    +1.3 dB bump at 125 Hz; level and phase within 0.4 dB.
constexpr size_t TOP_SECTIONS = 5;
constexpr FilterChain<TOP_SECTIONS> TOP{{
    {FilterKind::LOW_PASS, 14567.3f, 0.300f, 0.0f},
    {FilterKind::LOW_PASS, 9219.0f, 1.832f, 0.0f},
    {FilterKind::LOW_PASS, 12249.7f, 2.120f, 0.0f},
    {FilterKind::LOW_PASS, 17379.0f, 0.300f, 0.0f},
    {FilterKind::PEAK, 8688.2f, 6.852f, -3.53f},
}};
constexpr size_t LOW_CUT_SECTIONS = 2;
constexpr FilterChain<LOW_CUT_SECTIONS> LOW_CUT{{
    {FilterKind::HIGH_PASS, 90.5f, 1.072f, 0.0f},
    {FilterKind::HIGH_PASS, 37.3f, 0.616f, 0.0f},
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

// NAM-X's knobs are plain curves over the pedal's range, not its knobs' positions (the user,
// 2026-11-10; tools/capture/README.md, Knob curves). Depth: 0–1, linear in the sweep's swing
// at Rate 12:00 (0.03 to 2.4 ms), the minimum and the bend following along the pedal's own
// path. Level: the chorus re the dry in dB. These convert between the two scales (captures,
// and settings saved before preset version 5).
inline float depth_from_pedal(float position) {
    return (knob_map(SWING_MS, position) - SWING_MS.front()) / (SWING_MS.back() - SWING_MS.front());
}

// The pedal's Depth position (0–1 of its rotation) that gives NAM-X's `depth` (0–1).
inline float pedal_depth(float depth) {
    const float swing =
        SWING_MS.front() + std::clamp(depth, 0.0f, 1.0f) * (SWING_MS.back() - SWING_MS.front());
    size_t i = 0;
    while (i + 2 < SWING_MS.size() && swing > SWING_MS[i + 1]) {
        ++i;
    }
    const float t = (swing - SWING_MS[i]) / (SWING_MS[i + 1] - SWING_MS[i]);
    return KNOB_AT[i] + t * (KNOB_AT[i + 1] - KNOB_AT[i]);
}

// Low and High as cut frequencies (Hz), plain log curves; the pedal's positions on them.
// Low: where the chorus's bass is 3 dB down, per position of the pedal's knob (from the
// shelf above). From 5:00 to 3:00 the pedal's cut is a shelf at 103 Hz that only deepens (0 to
// −6 dB): that stretch runs from 50 Hz (Off) to 103 Hz here.
constexpr std::array<float, 9> LOW_CUT_AT{0.0f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 1.0f};
constexpr std::array<float, 9> LOW_CUT_HZ{771.0f, 726.0f, 547.0f, 412.0f, 312.0f,
                                          214.0f, 150.0f, 103.0f, 50.0f};
constexpr float HIGH_OFF_HZ = 20000.0f; // from here up the High filter is open

// y at x on a table of points, x through `fx` (the tables are monotonic either way).
template <size_t N, typename F>
inline float table_at(const std::array<float, N>& xs, const std::array<float, N>& ys, float x,
                      F fx) {
    const bool rising = xs.back() > xs.front();
    size_t i = 0;
    while (i + 2 < N && (rising ? x > xs[i + 1] : x < xs[i + 1])) {
        ++i;
    }
    const float t = std::clamp((fx(x) - fx(xs[i])) / (fx(xs[i + 1]) - fx(xs[i])), 0.0f, 1.0f);
    return ys[i] + t * (ys[i + 1] - ys[i]);
}

inline float log_of(float v) {
    return std::log(v);
}

// The pedal's Low position (0–1 of its rotation) for a low cut at `hz`, and back.
inline float low_position(float hz) {
    return table_at(LOW_CUT_HZ, LOW_CUT_AT, std::clamp(hz, LOW_CUT_HZ.back(), LOW_CUT_HZ.front()),
                    log_of);
}
inline float low_hz_from_pedal(float position) {
    std::array<float, 9> logs{};
    std::transform(LOW_CUT_HZ.begin(), LOW_CUT_HZ.end(), logs.begin(), log_of);
    return std::exp(table_at(LOW_CUT_AT, logs, std::clamp(position, 0.0f, 1.0f), [](float v) {
        return v;
    }));
}

// The pedal's High position for a high cut at `hz` (HIGH_OFF_HZ and up: open), and back.
inline float high_position(float hz) {
    if (hz >= HIGH_OFF_HZ) {
        return 1.0f;
    }
    return table_at(HIGH_HZ, KNOB_AT, std::max(hz, HIGH_HZ.front()), log_of);
}
inline float high_hz_from_pedal(float position) {
    return std::min(HIGH_OFF_HZ, knob_map_log(HIGH_HZ, position));
}

inline float level_db_from_pedal(float position) {
    return knob_map(ELEVEL_DB, position) - DRY_GAIN_DB;
}

// A curve given at RATE_HZ, at `rate_hz` (log-Hz interpolation, flat outside).
inline float rate_map(const std::array<float, 5>& map, float rate_hz) {
    const float x = std::log(std::max(rate_hz, 1e-3f));
    if (x <= std::log(RATE_HZ.front())) {
        return map.front();
    }
    for (size_t i = 0; i + 1 < RATE_HZ.size(); ++i) {
        const float a = std::log(RATE_HZ[i]), b = std::log(RATE_HZ[i + 1]);
        if (x <= b) {
            return map[i] + (x - a) / (b - a) * (map[i + 1] - map[i]);
        }
    }
    return map.back();
}

// The delay (ms, re the dry) at LFO phase `phase` for one Depth / Rate.
struct Sweep
{
    float low_ms = 0.0f; // the bottom of the swing
    float swing_ms = 0.0f;
    float bend = 0.0f;
    float bend_norm = 1.0f; // 1 / (e^bend − 1)
    float duty = 0.5f; // the share of the cycle the delay falls for

    // `depth_fraction`: the pedal's Depth position (pedal_depth() of NAM-X's knob).
    void set(float depth_fraction, float rate_hz) {
        // Above Depth 12:00 the rate's effect moves from the 12:00 curves to the 5:00 ones.
        const float full = std::clamp((depth_fraction - 0.5f) / 0.5f, 0.0f, 1.0f);
        const auto at_rate = [&](const std::array<float, 5>& mid, const std::array<float, 5>& top) {
            return rate_map(mid, rate_hz) +
                   full * (rate_map(top, rate_hz) - rate_map(mid, rate_hz));
        };
        swing_ms = knob_map(SWING_MS, depth_fraction) * at_rate(SWING_SCALE_MID, SWING_SCALE_FULL);
        bend = knob_map(BEND, depth_fraction) * at_rate(BEND_SCALE_MID, BEND_SCALE_FULL);
        bend_norm = std::abs(bend) > 1e-4f ? 1.0f / std::expm1(bend) : 0.0f;
        // The shift belongs to the swing: none without one (Depth 7:00 stands still).
        const float shift =
            at_rate(MIN_SHIFT_MS_MID, MIN_SHIFT_MS_FULL) * std::min(1.0f, depth_fraction / 0.5f);
        low_ms = knob_map(DELAY_MIN_MS, depth_fraction) + shift - DRY_LATENCY_MS - TONE_DELAY_MS;
        duty = rate_map(DUTY, rate_hz);
    }

    float delay_ms(float phase) const {
        const float u = phase < duty ? 1.0f - phase / duty : (phase - duty) / (1.0f - duty);
        const float v =
            (1.0f - ROUND) * u + ROUND * (0.5f - 0.5f * std::cos(std::numbers::pi_v<float> * u));
        return low_ms + swing_ms * (bend_norm > 0.0f ? std::expm1(bend * v) * bend_norm : v);
    }
};

// The Low and High knobs' filters at one sample rate.
struct Tone
{
    BiquadChain<TOP_SECTIONS> top;
    BiquadChain<LOW_CUT_SECTIONS> low_cut;
    BiquadChain<1> low;
    float high_k = 1.0f; // one-pole coefficient (1: flat)
    float high_gain = 1.0f;
    float low_fraction = -1.0f, high_fraction = -1.0f; // what `low` / `high_k` are set for

    void prepare(double sr) {
        top.design(TOP, sr);
        low_cut.design(LOW_CUT, sr);
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
    BiquadChainState<TOP_SECTIONS> top;
    BiquadChainState<LOW_CUT_SECTIONS> low_cut;
    BiquadChainState<1> low;
    float high = 0.0f;

    // The wet up to its clip: the wall, Low, High.
    float before_clip(float x, const Tone& k) {
        const float shaped = low.process(top.process(x, k.top), k.low);
        high += k.high_k * (shaped - high);
        return k.high_gain * high;
    }

    // After the clip: the output's low cut.
    float after_clip(float x, const Tone& k) {
        return low_cut.process(x, k.low_cut);
    }

    void reset() {
        top.reset();
        low_cut.reset();
        low.reset();
        high = 0.0f;
    }
};

} // namespace ce5

} // namespace namx
