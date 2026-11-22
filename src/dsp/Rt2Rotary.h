#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <numbers>
#include <span>
#include <vector>

#include "BiquadChain.h"
#include "Rt2Drive.h"
#include "Rt2RotaryData.h"

namespace namx::rt2
{

// The Rotary: a model of a Boss RT-2 Rotary Ensemble fitted to captures
// (captures/boss/rt-2/README.md). The pedal is digital, a rotating horn and drum heard by two
// microphones in a room:
//
//   input → hard clip (the input's ceiling) → Drive (Rt2Drive.h) →
//   crossover (high-pass to the horn, low-pass to the drum) →
//     horn:  the microphone's own direct path, its delay a V over one turn (the microphone
//            almost touches the horn's circle), plus the room's reflections, each a moving
//            delay d(θ) = T − S·cos 2π(θ − φ) of its own;
//     drum:  one moving path;
//   each path read at its delay, split into angle components (1, cos, sin of θ − φ …), each
//   component through its own measured filter, plus a fixed filter per rotor for what doesn't
//   move (Rt2RotaryData.h, fitted by tools/capture/fit_rt2.py) → the horn's high shelf →
//   Balance → Level.
//
// Output A is one microphone (also the pedal's mono output), B the other, 0.17 turn (62°)
// round the horn from it. The rotors turn at the Slow or Fast knob's speed, the drum a fixed
// ratio slower than the horn, and change speed with the pedal's inertia (Rise/Fall).

// The knobs' measured laws at the five clock positions (7:00, 9:00, 12:00, 3:00, 5:00).
inline constexpr std::array<float, 5> KNOB_AT{0.0f, 0.2f, 0.5f, 0.8f, 1.0f};
// The horn's speed (turns per second): Slow 7:00 stops the rotors.
inline constexpr std::array<float, 5> SLOW_HZ{0.0f, 0.2445f, 0.811f, 1.959f, 2.5f};
inline constexpr std::array<float, 5> FAST_HZ{2.5f, 4.099f, 6.845f, 9.024f, 10.0f};
// The drum turns this much slower than the horn (horn ÷ drum), slow and fast.
inline constexpr float DRUM_RATIO_SLOW = 1.0270f;
inline constexpr float DRUM_RATIO_FAST = 1.0871f;
// Level (dB re 12:00): 7:00 mutes, the rest a plain gain, flat within 0.1 dB.
inline constexpr std::array<float, 5> LEVEL_DB{-120.0f, -10.51f, 0.0f, 5.25f, 6.74f};
// Balance (dB re 12:00): each rotor's gain; 7:00 the drum alone, 5:00 the horn alone. The first
// session's chord tones per position, re the isolated session's 12:00 (the model's): its ends
// from noise (horn +6.2, drum +5.8); the first session's 12:00 had 1.5 dB less drum, 0.3 more horn.
inline constexpr std::array<float, 5> BALANCE_HORN_DB{-120.0f, -10.0f, 0.0f, 4.6f, 6.2f};
inline constexpr std::array<float, 5> BALANCE_DRUM_DB{5.8f, 4.3f, 0.0f, -9.6f, -120.0f};

// The input clips (hard) at +2.25 dB re the guitar's peak: clean at 0 dB, ~3 % THD at +3 dB.
// NAM-X's signal puts the guitar's peak at REFERENCE_DBFS.
inline constexpr float REFERENCE_DBFS = -6.0f;
inline constexpr float INPUT_CEILING_DB = 2.25f;

// Speed changes (Rise/Fall): a rotor speeding up approaches its target exponentially;
// slowing down, the drum coasts (its speed decays towards 0) until it meets the slow speed,
// the horn is braked to it. Time constants in seconds, slews in Hz per second.
struct Inertia
{
    float up_s; // exponential approach, speeding up (0: slew instead)
    float up_slew; // Hz/s
    float down_s; // coasting towards 0 (0: slew instead)
    float down_slew;
};
inline constexpr Inertia HORN_FAST_RAMP{0.25f, 0.0f, 0.0f, 25.0f};
inline constexpr Inertia HORN_SLOW_RAMP{0.0f, 2.0f, 0.0f, 2.0f};
inline constexpr Inertia DRUM_FAST_RAMP{1.0f, 0.0f, 1.0f, 0.0f};
inline constexpr Inertia DRUM_SLOW_RAMP{13.6f, 0.0f, 9.0f, 0.0f};

// A knob position (0–1) through one of the five-point tables, linearly between points.
inline float knob_law(const std::array<float, 5>& values, float pos) {
    pos = std::clamp(pos, 0.0f, 1.0f);
    size_t i = 0;
    while (i + 2 < KNOB_AT.size() && pos > KNOB_AT[i + 1]) {
        ++i;
    }
    const float t = (pos - KNOB_AT[i]) / (KNOB_AT[i + 1] - KNOB_AT[i]);
    return values[i] + t * (values[i + 1] - values[i]);
}

// A gain table in dB whose ends mute: the outermost segments fade in linear amplitude.
inline float knob_gain(const std::array<float, 5>& db, float pos) {
    auto amp = [](float d) {
        return d <= -100.0f ? 0.0f : std::pow(10.0f, d / 20.0f);
    };
    pos = std::clamp(pos, 0.0f, 1.0f);
    if (db.front() <= -100.0f && pos < KNOB_AT[1]) {
        return amp(db[1]) * pos / KNOB_AT[1];
    }
    if (db.back() <= -100.0f && pos > KNOB_AT[3]) {
        return amp(db[3]) * (1.0f - pos) / (1.0f - KNOB_AT[3]);
    }
    return amp(knob_law(db, pos));
}

// Everything the engine needs per block, from the knobs (the processor builds it).
struct Settings
{
    int mode = 0; // 0–2: Mode I, II, III
    float slow_hz = 0.811f; // the horn's speed at slow and at fast
    float fast_hz = 6.845f;
    bool fast = false; // the LED red: rotors fast
    bool slow_ramps = false; // Rise/Fall SLOW
    float drive = 0.0f; // 0–1 of the knob
    float horn_gain = 1.0f; // Balance
    float drum_gain = 1.0f;
    float level = 1.0f;
};

// audio thread (all but prepare)
inline float dot(const float* a, const float* b, size_t n) {
    // Eight running sums: the compiler vectorizes the inner loop (n is a multiple of 8).
    std::array<float, 8> acc{};
    for (size_t i = 0; i < n; i += 8) {
        for (size_t k = 0; k < 8; ++k) {
            acc[k] += a[i + k] * b[i + k];
        }
    }
    float sum = 0.0f;
    for (float v : acc) {
        sum += v;
    }
    return sum;
}

class Engine
{
public:
    static constexpr size_t CHUNK = 64;

    // Allocates and resamples the measured filters to the rate: not on the audio thread.
    void prepare(double rate);
    void reset();
    // The rotors' angles (turns) and speeds (turns per second), for offline renders that
    // start where a capture did (tools/capture/render_rt2.cpp).
    void set_rotors(double horn_turns, double drum_turns, float horn_hz, float drum_hz) {
        horn_angle = horn_turns - std::floor(horn_turns);
        drum_angle = drum_turns - std::floor(drum_turns);
        horn_speed = horn_hz;
        drum_speed = drum_hz;
    }
    // Mono input; writes Output A to `a` and, when `b` isn't null, Output B.
    void process(const float* in, float* a, float* b, size_t n, const Settings& s);
    // The delay the Drive adds (its oversampling filters), in samples: the same in every
    // mode and at every position of the knob.
    static constexpr double latency_samples() {
        return Drive::latency_samples();
    }

private:
    // A filter at the engine's rate, reversed for dot(), its length a multiple of 8.
    struct Filter
    {
        std::vector<float> taps;
    };
    // A bank: components filtered in low rank (Bank in Rt2RotaryTables.h), per output.
    struct LowRank
    {
        size_t rank = 0;
        size_t comps = 0;
        size_t length = 0; // the basis filters' (equal) length
        std::vector<float> gains_t; // rank × comps
        std::vector<Filter> basis; // rank
        size_t history = 0; // its rank sums' buffers start here in `sums`
        size_t first_row = 0; // its components' first row in `comps`
        int output = -1; // the output it feeds (-1: both)
        bool drum = false;
    };
    // A moving path: where it reads (its delay, ms, per angle) and which bank it feeds.
    struct Path
    {
        bool horn = true;
        bool table = false; // delay from a table (direct, drum) or T − S·cos
        const float* curve = nullptr;
        float t_ms = 0.0f, swing_ms = 0.0f;
        float cos_phi = 1.0f, sin_phi = 0.0f;
        size_t order = 1; // angle components: 1, cos, sin … up to this harmonic
        size_t advance = 0; // samples read ahead (the filters' negative lags)
    };
    struct Mode
    {
        std::vector<Path> paths;
        std::vector<LowRank> banks;
        size_t rows = 0; // components per sample, all paths
        Filter horn_static[2], drum_static;
        size_t static_length = 0;
        // The crossover ahead of each rotor, and the horn's shelf after it per output.
        BiquadChain<2> horn_cross, drum_cross;
        std::array<BiquadChain<1>, 2> horn_shelf;
    };

    static Filter resample(std::span<const float> taps48, int lag0, double rate, size_t advance);
    LowRank make_bank(const Bank& b, size_t comps, size_t taps48, int lag0, size_t advance) const;
    void build(size_t index, const ModeTables& t);
    float curve_at(const float* curve, float turns) const;
    void turn_rotors(const Settings& s, size_t n);
    float drive_sample(float x);

    double fs = 48000.0;
    std::array<Mode, 3> modes;
    int active = -1;

    // The input after the clip, Drive and each side of the crossover: power-of-two rings for the
    // horn's and the drum's paths' reads.
    std::vector<float> horn_line, drum_line;
    size_t mask = 0, write = 0;

    // Per bank and rank: its last (length − 1) sums, then this chunk's.
    std::vector<float> sums;
    // This chunk's components, path by path: rows × CHUNK.
    std::vector<float> comps;

    // The fixed filters' inputs: each side of the crossover's history, then this chunk's.
    std::vector<float> horn_plain, drum_plain;

    // The crossover's and the horn's shelves' state.
    BiquadChainState<2> horn_cross_state, drum_cross_state;
    std::array<BiquadChainState<1>, 2> shelf_state;

    Drive drive;

    // Rotors: angle (turns) and speed (turns per second), per sample of the chunk.
    double horn_angle = 0.0, drum_angle = 0.0;
    float horn_speed = 0.811f, drum_speed = 0.811f / DRUM_RATIO_SLOW;
    std::array<float, CHUNK> horn_cos{}, horn_sin{}, drum_cos{}, drum_sin{};
    std::array<float, CHUNK> horn_turns{}, drum_turns{};
    std::array<float, CHUNK> x_chunk{};
    // Per path, this chunk: the rotor's travel over the path's read-ahead (turns, cos, sin).
    static constexpr size_t MAX_PATHS = 48;
    std::array<float, MAX_PATHS> ahead_turns{}, ahead_cos{}, ahead_sin{};
};

} // namespace namx::rt2
