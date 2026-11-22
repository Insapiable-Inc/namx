#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "Common.h"

namespace namx
{

// The Boss PH-3 Phase Shifter, every mode, fitted to captures of a real one (2026-10-28 and the
// focused session of 2026-11-10; the study and its data are in captures/boss/ph-3/README.md,
// tools/capture/ph3/). A digital phaser. Its Stage modes are chains of identical first-order
// all-pass stages (90° at their corner), found on nine tones followed through the whole sweep:
//
//   4 Stage    out = TONE · g · (v + A⁴(v))                    four stages, one corner fc(t)
//   8 Stage    six sweeping stages and two parked at 4.3 kHz
//   10 Stage   six sweeping stages and four parked at 7.65 kHz
//   12 Stage   a 6 Stage phaser run twice: (g · (1 + A⁶))²
//   fc(t)      the mode's own measured curve from its low to its high, read at
//              u = rest + D(Depth) · (tri − rest): Depth shrinks the LFO's travel about the
//              point Depth 7:00 rests on; fast Rates shrink the triangle's travel first
//   Res        v = x + k(Res) · HP( z⁻¹ · chain(v) )  — the loop round the whole chain, the dry
//              inside it (12 Stage: a loop in each pass, its dry outside). 8 Stage's passes one
//              more stage parked at 8.2 kHz on the way back (the tones with Res up: 1.1 dB;
//              3.1 without it). 4 Stage's closes within the sample (the pedal shows no delay
//              in it) and is still not the pedal's: see the study.
//   Step       the 8 Stage sweep, its LFO sampled and held (hold from the Rate)
//   Rise / Fall   a barber pole: notches 1.88 octaves apart sliding one spacing per LFO
//                 period; Depth sets how deep the notches are and how loud it is (below). Not the pedal's structure (a ladder of
//                 second-order sections, re-seated from one end to the other).
//
// With Res off the stage chains follow the pedal's nine tones over the whole cycle within
// 0.1–0.9 dB (the second-order sections they replace: 2–8 dB) and its standing sweeps within
// 0.1–0.6 dB. The pedal runs at 44.1 kHz; here the stages are designed at the engine's rate
// with the same corners, which moves the notches above ~6 kHz by a few per cent.
namespace ph3
{

enum Mode : int
{
    STAGE_4 = 0,
    STAGE_8,
    STAGE_10,
    STAGE_12,
    RISE,
    FALL,
    STEP,
};
constexpr int MODE_COUNT = 7;

struct Section
{
    float hz = 0.0f;
    float q = 0.7f;
};

constexpr size_t MAX_SECTIONS = 6; // 12 Stage: two passes of three; the Rise/Fall ladder

struct Voicing
{
    std::array<Section, MAX_SECTIONS> sections{};
    size_t count = 0; // per pass
    float low = 1.0f, high = 1.0f; // the section scale's range at full Depth and a slow Rate
    float rest = 1.0f; // … and where Depth 7:00 leaves it (the static filter)
    float dry = 1.0f, wet = 1.0f; // the mix (Res off)
    float hp_hz = 0.0f; // TONE: a one-pole high-pass …
    float shelf_hz = 0.0f; // … and a first-order high shelf
    float shelf_db = 0.0f;
    float k_full = 0.0f; // the loop's gain at Res 5:00
    float s_full = 0.0f; // the output's scale at Res 5:00
    float loop_hp_hz = 0.0f; // the loop's DC blocker
    bool twin = false; // 12 Stage
    bool ladder = false; // Rise / Fall
    // The Stage modes (not the ladder): identical first-order all-pass stages.
    size_t stages = 0; // sweeping, per pass
    size_t parked_count = 0; // fixed stages after them
    float parked_hz = 0.0f;
    float loop_parked_hz = 0.0f; // 8 Stage: one more fixed stage, in the loop only (0 = none)
    bool loop_direct = false; // 4 Stage: the loop closes within the sample (no sample of delay)
    float gain = 1.0f; // dry = wet (per pass)
    float rest_hz = 0.0f; // the corner at Depth 7:00
    float low_hz = 0.0f, high_hz = 0.0f; // … and its ends at Depth 5:00, a slow Rate
    // Where the corner sits between its ends (in log) along the LFO's triangle, 0 to 1 in 16 steps.
    std::array<float, 17> shape{};
};

// The Stage modes, from the focused session's tones and standing sweeps (2026-11-10). The
// loops (k_full, s_full, loop_hp_hz) are October's.
constexpr Voicing STAGE_4_VOICING{
    .hp_hz = 62.7f,
    .k_full = 0.664f,
    .s_full = 0.742f,
    .loop_hp_hz = 89.0f,
    .stages = 4,
    .loop_direct = true,
    .gain = 0.798f,
    .rest_hz = 371.6f,
    .low_hz = 84.3f,
    .high_hz = 4543.9f,
    .shape = {0.0000f, 0.0661f, 0.1632f, 0.2627f, 0.3545f, 0.4370f, 0.5110f, 0.5779f, 0.6388f,
              0.6946f, 0.7465f, 0.7950f, 0.8406f, 0.8839f, 0.9250f, 0.9644f, 1.0000f},
};
constexpr Voicing STAGE_8_VOICING{
    .hp_hz = 228.1f,
    .k_full = 0.743f,
    .s_full = 0.749f,
    .loop_hp_hz = 242.0f,
    .stages = 6,
    .parked_count = 2,
    .parked_hz = 4345.0f,
    .loop_parked_hz = 8169.0f,
    .gain = 0.86f,
    .rest_hz = 638.0f,
    .low_hz = 278.7f,
    .high_hz = 4624.8f,
    .shape = {0.0000f, 0.0254f, 0.0706f, 0.1231f, 0.1805f, 0.2416f, 0.3059f, 0.3733f, 0.4432f,
              0.5154f, 0.5893f, 0.6646f, 0.7407f, 0.8168f, 0.8914f, 0.9608f, 1.0000f},
};
constexpr Voicing STAGE_10_VOICING{
    .hp_hz = 63.0f,
    .k_full = 0.681f,
    .s_full = 0.877f,
    .loop_hp_hz = 54.9f,
    .stages = 6,
    .parked_count = 4,
    .parked_hz = 7650.0f,
    .gain = 0.765f,
    .rest_hz = 802.0f,
    .low_hz = 365.6f,
    .high_hz = 9765.9f,
    .shape = {0.0000f, 0.0366f, 0.0988f, 0.1705f, 0.2459f, 0.3214f, 0.3954f, 0.4674f, 0.5371f,
              0.6048f, 0.6705f, 0.7345f, 0.7967f, 0.8566f, 0.9143f, 0.9668f, 1.0000f},
};
constexpr Voicing STAGE_12_VOICING{
    .hp_hz = 62.8f,
    .k_full = 0.706f,
    .s_full = 0.58f,
    .loop_hp_hz = 103.0f,
    .twin = true,
    .stages = 6,
    .gain = 0.64f,
    .rest_hz = 1735.1f,
    .low_hz = 502.8f,
    .high_hz = 9288.8f,
    .shape = {0.0000f, 0.1030f, 0.2195f, 0.3297f, 0.4236f, 0.5116f, 0.5863f, 0.6555f, 0.7195f,
              0.7730f, 0.8225f, 0.8673f, 0.9053f, 0.9373f, 0.9635f, 0.9857f, 1.0000f},
};

// Rise / Fall: LADDER sections LADDER_OCTAVES apart from LADDER_BOTTOM_HZ, 4 Stage's mix
// and tone, 10 Stage's loop. Each section fades in over the bottom spacing and out over the
// top one (blended with its input), so it wraps from one end to the other unheard.
constexpr size_t LADDER = 6;
constexpr float LADDER_OCTAVES = 1.88f;
constexpr float LADDER_BOTTOM_HZ = 8.0f; // the top section reaches 19.9 kHz
constexpr float LADDER_Q = 0.62f;
constexpr Voicing LADDER_VOICING{
    .count = LADDER,
    .low = 1.0f,
    .high = 1.0f,
    .rest = 1.0f,
    .dry = 0.757f,
    .wet = 0.757f,
    .hp_hz = 52.3f,
    .shelf_hz = 2695.0f,
    .shelf_db = 0.35f,
    .k_full = 0.681f,
    .s_full = 0.877f,
    .loop_hp_hz = 54.9f,
    .ladder = true,
};

inline const Voicing& voicing(int mode) {
    switch (mode) {
        case STAGE_8:
        case STEP:
            return STAGE_8_VOICING;
        case STAGE_10:
            return STAGE_10_VOICING;
        case STAGE_12:
            return STAGE_12_VOICING;
        case RISE:
        case FALL:
            return LADDER_VOICING;
        default:
            return STAGE_4_VOICING;
    }
}

// Knob maps at 7:00, 9:00, 12:00, 3:00, 5:00 (fraction of the rotation), linear in between.
constexpr std::array<float, 5> KNOB_AT{0.0f, 0.2f, 0.5f, 0.8f, 1.0f};

// Depth shrinks the LFO's travel about the point it rests on: the corner always follows the
// mode's one curve, read at  u = rest + D · (tri − rest). D = 1 at 5:00; at 12:00 the focused
// session's tones give 0.48 from either end of the sweep (to 0.4 % of the curve); 9:00 and 3:00
// are October's (rougher: 0.07–0.17 and 0.72–0.85 from the two ends).
constexpr std::array<float, 5> DEPTH_TRAVEL{0.0f, 0.12f, 0.48f, 0.78f, 1.0f};

// Fast Rates shrink the travel and raise its centre (4 Stage, Depth 5:00; October's): the
// part of the triangle's travel that is left, from and to.
constexpr std::array<float, 3> FAST_HZ{4.64f, 12.1f, 21.5f};
constexpr std::array<float, 3> FAST_FROM{0.0f, 0.207f, 0.332f};
constexpr std::array<float, 3> FAST_TO{1.0f, 0.929f, 0.862f};

// Rise / Fall: Depth never changes the speed, but it changes the mix (the session of
// 2026-11-11: nine tones at each of Depth's positions). Fully up the dry and the wet are
// equal: deep notches, about bypass level. Turned down both grow and the dry more, and the
// wet loses its top: at 7:00 shallow notches, 5 dB louder, nothing moving above 4 kHz. The
// gains put each position's loudness step re 5:00 on the pedal's (tones within 0.2 dB, the
// guitar at 7:00 0.5 dB under); 5:00 is the October fit.
constexpr std::array<float, 5> LADDER_DRY{1.677f, 1.342f, 1.029f, 0.824f, 0.757f};
constexpr std::array<float, 5> LADDER_WET{1.117f, 1.188f, 1.029f, 0.824f, 0.757f};
constexpr std::array<float, 5> LADDER_WET_TOP_HZ{3800.0f, 4400.0f, 5800.0f, 13500.0f, 30000.0f};
constexpr float LADDER_WET_TOP_OFF_HZ = 20000.0f; // from here up the wet's low-pass is out

// Res: the loop's gain (8 Stage's ladder; ≈ 0.7 · fraction^0.6), scaled to each mode's k_full.
constexpr std::array<float, 5> RES_K{0.0f, 0.245f, 0.443f, 0.634f, 0.698f};
// The most the loop's gain may be, in any mode: just over the largest k_full (8 Stage's).
constexpr float LOOP_GAIN_TOP = 0.75f;

// Step: the hold against the LFO's rate; above the last, the LFO period / 8.
constexpr std::array<float, 5> STEP_HZ{0.047f, 0.225f, 0.394f, 0.654f, 1.77f};
constexpr std::array<float, 5> STEP_MS{350.0f, 178.0f, 121.0f, 86.0f, 71.0f};

inline float knob_map(const std::array<float, 5>& map, float fraction) {
    const float x = std::clamp(fraction, 0.0f, 1.0f);
    size_t i = 0;
    while (i + 2 < KNOB_AT.size() && x > KNOB_AT[i + 1]) {
        ++i;
    }
    const float t = (x - KNOB_AT[i]) / (KNOB_AT[i + 1] - KNOB_AT[i]);
    return map[i] + t * (map[i + 1] - map[i]);
}

// y at x on a table, interpolating in log x (x clamped to the table's ends).
template <size_t N>
float log_interpolate(const std::array<float, N>& xs, const std::array<float, N>& ys, float x,
                      bool log_y) {
    const float lx = std::log(std::clamp(x, xs.front(), xs.back()));
    size_t i = 0;
    while (i + 2 < N && lx > std::log(xs[i + 1])) {
        ++i;
    }
    const float t = (lx - std::log(xs[i])) / (std::log(xs[i + 1]) - std::log(xs[i]));
    if (log_y) {
        return std::exp(std::log(ys[i]) + t * (std::log(ys[i + 1]) - std::log(ys[i])));
    }
    return ys[i] + t * (ys[i + 1] - ys[i]);
}

// NAM-X's knobs are plain curves, not the pedal's rotation: Depth is the share of the travel
// (D), Feedback the loop's gain per pass (k), the same in every mode. These give the value
// that sounds like a position (0–1 = 7:00–5:00) on the pedal's Depth and Res (older presets
// were saved that way), and the Depth position a travel stands for (Rise and Fall's mix is
// tabulated by position: the knob walks the pedal's path).
inline float depth_from_pedal(float position) {
    return knob_map(DEPTH_TRAVEL, position);
}

inline float depth_position(float travel) {
    const float d = std::clamp(travel, 0.0f, 1.0f);
    size_t i = 0;
    while (i + 2 < DEPTH_TRAVEL.size() && d > DEPTH_TRAVEL[i + 1]) {
        ++i;
    }
    const float t = (d - DEPTH_TRAVEL[i]) / (DEPTH_TRAVEL[i + 1] - DEPTH_TRAVEL[i]);
    return KNOB_AT[i] + t * (KNOB_AT[i + 1] - KNOB_AT[i]);
}

inline float feedback_from_pedal(const Voicing& v, float position) {
    return knob_map(RES_K, position) / RES_K.back() * v.k_full;
}

inline float step_hold_ms(float rate_hz) {
    if (rate_hz > STEP_HZ.back()) {
        return 1000.0f / (8.0f * rate_hz);
    }
    return log_interpolate(STEP_HZ, STEP_MS, rate_hz, true);
}

inline float triangle(float phase) {
    const float p = phase - std::floor(phase);
    return 1.0f - std::abs(2.0f * p - 1.0f);
}

// The sweeping stages' corner (Hz) for one setting and LFO position (tri 0–1).
struct Sweep
{
    float low_hz = 1.0f;
    float log_span = 0.0f;
    float rest = 0.0f; // where on the triangle's travel the mode's rest corner lies
    float travel = 0.0f; // Depth
    float from = 0.0f, to = 1.0f; // the fast-Rate range
    const std::array<float, 17>* shape = nullptr;

    void set(const Voicing& v, float new_travel, float rate_hz) {
        shape = &v.shape;
        low_hz = v.low_hz;
        log_span = std::log(v.high_hz / v.low_hz);
        travel = std::clamp(new_travel, 0.0f, 1.0f);
        from = log_interpolate(FAST_HZ, FAST_FROM, rate_hz, false);
        to = log_interpolate(FAST_HZ, FAST_TO, rate_hz, false);
        // The rest corner's place on the curve, turned back into a position on the travel.
        const float place = std::log(v.rest_hz / v.low_hz) / log_span;
        size_t i = 0;
        while (i + 2 < v.shape.size() && place > v.shape[i + 1]) {
            ++i;
        }
        rest =
            (static_cast<float>(i) + (place - v.shape[i]) / (v.shape[i + 1] - v.shape[i])) / 16.0f;
    }

    float corner_hz(float tri) const {
        if (shape == nullptr) {
            return low_hz;
        }
        const float u = rest + travel * (from + (to - from) * std::clamp(tri, 0.0f, 1.0f) - rest);
        const float x = std::clamp(u, 0.0f, 1.0f) * 16.0f;
        const size_t i = std::min<size_t>(static_cast<size_t>(x), 15);
        const float place =
            (*shape)[i] + (x - static_cast<float>(i)) * ((*shape)[i + 1] - (*shape)[i]);
        return low_hz * std::exp(log_span * place);
    }
};

// One first-order all-pass stage, (a + z⁻¹) / (1 + a z⁻¹); its coefficient is shared (and
// glides) across a chain.
struct Stage
{
    float x1 = 0.0f, y1 = 0.0f;

    float process(float x, float a) {
        const float y = a * x + x1 - a * y1;
        x1 = x;
        y1 = y;
        return y;
    }

    // Its output for x without moving on.
    float peek(float x, float a) const {
        return a * x + x1 - a * y1;
    }

    void clear() {
        x1 = y1 = 0.0f;
    }
};

// The coefficient that puts a stage's 90° at hz.
inline float stage_coefficient(float hz, float fs) {
    const float t = std::tan(std::numbers::pi_v<float> * std::clamp(hz, 10.0f, 0.45f * fs) / fs);
    return (t - 1.0f) / (t + 1.0f);
}

// A second-order all-pass, direct form I (stays all-pass while its coefficients glide).
struct AllPass
{
    float a1 = 0.0f, a2 = 0.0f;
    float step1 = 0.0f, step2 = 0.0f; // per-sample glide toward the next control point
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

    float process(float x) {
        a1 += step1;
        a2 += step2;
        const float y = a2 * x + a1 * x1 + x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }

    void clear() {
        x1 = x2 = y1 = y2 = 0.0f;
    }

    // As if it had passed its input straight through: what an all-pass far below or above
    // the band nearly does (Rise / Fall re-seat sections there; clearing them clicked).
    void seat() {
        y1 = x1;
        y2 = x2;
    }

    // RBJ's all-pass at hz, glided to over `samples` (0 = at once).
    void target(float hz, float q, float fs, int samples) {
        const float top = std::min(21900.0f, 0.46f * fs);
        const float w = 2.0f * std::numbers::pi_v<float> * std::clamp(hz, 1.0f, top) / fs;
        const float alpha = std::sin(w) / (2.0f * q);
        const float t1 = -2.0f * std::cos(w) / (1.0f + alpha);
        const float t2 = (1.0f - alpha) / (1.0f + alpha);
        if (samples <= 0) {
            a1 = t1;
            a2 = t2;
            step1 = step2 = 0.0f;
        } else {
            step1 = (t1 - a1) / static_cast<float>(samples);
            step2 = (t2 - a2) / static_cast<float>(samples);
        }
    }
};

// A first-order section (bilinear designs).
struct FirstOrder
{
    float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f;
    float x1 = 0.0f, y1 = 0.0f;

    float process(float x) {
        const float y = b0 * x + b1 * x1 - a1 * y1;
        x1 = x;
        y1 = y;
        return y;
    }

    void clear() {
        x1 = y1 = 0.0f;
    }

    void high_pass(float hz, float fs) {
        const float k = std::tan(std::numbers::pi_v<float> * hz / fs);
        b0 = 1.0f / (1.0f + k);
        b1 = -b0;
        a1 = -(1.0f - k) / (1.0f + k);
    }

    // Unity at DC, `db` at Nyquist, corner at hz.
    void high_shelf(float hz, float db, float fs) {
        const float g = std::pow(10.0f, db / 20.0f);
        const float k = std::tan(std::numbers::pi_v<float> * std::min(hz, 0.45f * fs) / fs);
        b0 = (g + k) / (1.0f + k);
        b1 = (k - g) / (1.0f + k);
        a1 = (k - 1.0f) / (1.0f + k);
    }
};

// A second-order Butterworth low-pass (RBJ), or nothing (`off`).
struct LowPass
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    bool off = true;

    float process(float x) {
        const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return off ? x : y;
    }

    void clear() {
        x1 = x2 = y1 = y2 = 0.0f;
    }

    void design(float hz, float fs) {
        const float w = 2.0f * std::numbers::pi_v<float> * std::min(hz, 0.45f * fs) / fs;
        const float alpha = std::sin(w) / std::numbers::sqrt2_v<float>;
        const float a0 = 1.0f + alpha;
        b1 = (1.0f - std::cos(w)) / a0;
        b0 = b2 = 0.5f * b1;
        a1 = -2.0f * std::cos(w) / a0;
        a2 = (1.0f - alpha) / a0;
    }
};

// One channel of the pedal.
struct Voice
{
    std::array<AllPass, 2 * MAX_SECTIONS> ap{}; // Rise / Fall: the ladder
    std::array<Stage, 12> stage{}; // the Stage modes' sweeping stages (12 Stage: 0–5, 6–11)
    std::array<Stage, 4> parked{}; // … and the fixed ones after them
    Stage loop_parked; // 8 Stage: the fixed stage on the way back
    float coefficient = 0.0f, coefficient_step = 0.0f; // the sweeping stages', gliding
    float parked_coefficient = 0.0f;
    float loop_parked_coefficient = 0.0f;
    FirstOrder hp, shelf;
    std::array<FirstOrder, 2> loop_hp{}; // twin: one loop per pass
    std::array<float, 2> fb{};
    std::array<float, LADDER> exponent{}; // Rise / Fall: each section's place on the ladder
    std::array<float, LADDER> weight{}; // … and how much of it is heard (0 at the ends)
    float held = 0.0f; // Step: the sampled LFO
    LowPass ladder_top; // Rise / Fall: the wet's low-pass (Depth)
    float ladder_dry = LADDER_DRY.back(), ladder_wet = LADDER_WET.back(); // … and its mix
    float ladder_dry_target = LADDER_DRY.back(), ladder_wet_target = LADDER_WET.back();

    void reset() {
        for (AllPass& s : ap) {
            s.clear();
        }
        for (Stage& st : stage) {
            st.clear();
        }
        for (Stage& st : parked) {
            st.clear();
        }
        loop_parked.clear();
        hp.clear();
        ladder_top.clear();
        shelf.clear();
        for (FirstOrder& f : loop_hp) {
            f.clear();
        }
        fb = {};
    }

    // Filters that don't move: TONE and the loops' high-passes.
    void design(const Voicing& v, float fs) {
        hp.high_pass(v.hp_hz, fs);
        shelf.high_shelf(v.shelf_hz, v.shelf_db, fs);
        for (FirstOrder& f : loop_hp) {
            f.high_pass(v.loop_hp_hz, fs);
        }
        if (v.parked_count > 0) {
            parked_coefficient = stage_coefficient(v.parked_hz, fs);
        }
        if (v.loop_parked_hz > 0.0f) {
            loop_parked_coefficient = stage_coefficient(v.loop_parked_hz, fs);
        }
    }

    // The sweeping stages' corner at hz, glided to over `samples` (0 = at once).
    void sweep_to(float hz, float fs, int samples) {
        const float target = stage_coefficient(hz, fs);
        if (samples <= 0) {
            coefficient = target;
            coefficient_step = 0.0f;
        } else {
            coefficient_step = (target - coefficient) / static_cast<float>(samples);
        }
    }

    // Rise / Fall: the mix and the wet's low-pass at a position of the pedal's Depth
    // (`at_once`: no glide of the gains).
    void ladder_depth(float depth_fraction, float fs, bool at_once) {
        ladder_dry_target = knob_map(LADDER_DRY, depth_fraction);
        ladder_wet_target = knob_map(LADDER_WET, depth_fraction);
        if (at_once) {
            ladder_dry = ladder_dry_target;
            ladder_wet = ladder_wet_target;
        }
        const float top = knob_map(LADDER_WET_TOP_HZ, depth_fraction);
        ladder_top.off = top >= LADDER_WET_TOP_OFF_HZ;
        if (!ladder_top.off) {
            ladder_top.design(top, fs);
        }
    }

    // Rise / Fall: the ladder at `position` (in spacings, 0–LADDER); a section that wrapped
    // round to the other end starts there at once, seated.
    void ladder_to(float position, float fs, int samples) {
        for (size_t i = 0; i < LADDER; ++i) {
            float e = std::fmod(static_cast<float>(i) + position, static_cast<float>(LADDER));
            if (e < 0.0f) {
                e += static_cast<float>(LADDER);
            }
            const bool wrapped = std::abs(e - exponent[i]) > 0.5f * static_cast<float>(LADDER);
            exponent[i] = e;
            const float edge = std::clamp(std::min(e, static_cast<float>(LADDER) - e), 0.0f, 1.0f);
            weight[i] = edge * edge * (3.0f - 2.0f * edge);
            const float hz = LADDER_BOTTOM_HZ * std::exp2(LADDER_OCTAVES * e);
            if (wrapped) {
                ap[i].seat();
                ap[i].target(hz, LADDER_Q, fs, 0);
            } else {
                ap[i].target(hz, LADDER_Q, fs, samples);
            }
        }
    }

    float process(float x, const Voicing& v, float k, float s) {
        if (v.ladder) {
            const float in = x + k * loop_hp[0].process(fb[0]);
            float a = in;
            for (size_t i = 0; i < LADDER; ++i) {
                a += weight[i] * (ap[i].process(a) - a);
            }
            fb[0] = a;
            ladder_dry += 0.002f * (ladder_dry_target - ladder_dry);
            ladder_wet += 0.002f * (ladder_wet_target - ladder_wet);
            return s *
                   shelf.process(hp.process(ladder_dry * in + ladder_wet * ladder_top.process(a)));
        }
        coefficient += coefficient_step;
        float core;
        if (v.twin) {
            float u = x;
            for (size_t p = 0; p < 2; ++p) {
                float w = u + k * loop_hp[p].process(fb[p]);
                for (size_t i = 0; i < v.stages; ++i) {
                    w = stage[p * v.stages + i].process(w, coefficient);
                }
                fb[p] = w;
                u = v.gain * (u + w);
            }
            core = u;
        } else {
            float in;
            if (v.loop_direct) {
                // The loop without a sample of delay, solved: the chain puts out
                // gain · in + rest (rest: what its states alone would), the loop's high-pass
                // b0 · that + held.
                float rest = 0.0f;
                float gain = 1.0f;
                for (size_t i = 0; i < v.stages; ++i) {
                    rest = stage[i].peek(rest, coefficient);
                    gain *= coefficient;
                }
                for (size_t i = 0; i < v.parked_count; ++i) {
                    rest = parked[i].peek(rest, parked_coefficient);
                    gain *= parked_coefficient;
                }
                const FirstOrder& f = loop_hp[0];
                const float held = f.b1 * f.x1 - f.a1 * f.y1;
                in = (x + k * (f.b0 * rest + held)) / (1.0f - k * f.b0 * gain);
            } else {
                in = x + k * loop_hp[0].process(fb[0]);
            }
            float a = in;
            for (size_t i = 0; i < v.stages; ++i) {
                a = stage[i].process(a, coefficient);
            }
            for (size_t i = 0; i < v.parked_count; ++i) {
                a = parked[i].process(a, parked_coefficient);
            }
            if (v.loop_direct) {
                loop_hp[0].process(a);
            } else {
                fb[0] =
                    v.loop_parked_hz > 0.0f ? loop_parked.process(a, loop_parked_coefficient) : a;
            }
            core = v.gain * (in + a);
        }
        return s * hp.process(core);
    }
};

} // namespace ph3

} // namespace namx
