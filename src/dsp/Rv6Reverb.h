#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

#include "BiquadChain.h"
#include "Common.h"
#include "DelayLine.h"
#include "ReverbAlgorithms.h"
#include "Rv6Early.h"
#include "Rv6EarlyData.h"
#include "Rv6Shimmer.h"

namespace namx
{

// The Boss RV-6 Reverb's Modulate, Spring, Plate, Hall, Room and Shimmer modes, fitted to
// captures of a real one (2026-10-31; the study is captures/boss/rv-6/README.md, the fit
// tools/capture/fit_rv6.py). The five tank and spring modes share this frame:
//
//   out = dry + Level · Tone( tank(dry) )     the dry is the analog bypass: unity, no delay
//
//   Level  the wet's gain: the knob's square from 12:00 up (5:00 is 12 dB over 12:00), a
//          gentler power below it (9:00 is 13.5 dB under 12:00), 7:00 off;
//   Tone   one filter after the tank, the same in every mode, measured at 7:00, 9:00, 3:00
//          and 5:00 (12:00 is flat) and interpolated between them; below 12:00, in Plate,
//          Hall and Room, the pedal also adds one low reverb to both outputs alike (LOW);
//   Time   the tank's RT60 at 1 kHz by the mode's five-point law (7:00 to 9:00 barely
//          moves it), the frequency shape of noon, and the wet scaled so its energy stays
//          where the pedal keeps it (within ~3 dB of noon's).
//
// Room, Hall, Plate and Modulate use feedback delay networks; Spring uses dispersive
// all-passes. Shimmer has its own capture-fitted mono octave paths, shared modulation
// clock and stereo output response (Rv6Shimmer). All six mix with the unity dry path.
// prepare() allocates (GUI thread / audio stopped); everything else doesn't.
namespace rv6
{

enum class Mode : int
{
    MODULATE,
    SPRING,
    PLATE,
    HALL,
    ROOM,
    SHIMMER
};
constexpr size_t MODE_COUNT = 6;

// The knob positions the laws were measured at: 7:00, 9:00, 12:00, 3:00, 5:00.
constexpr array<float, 5> KNOB_AT{0.0f, 0.2f, 0.5f, 0.8f, 1.0f};

// A five-point table over the knob (fraction of the turn), piecewise linear.
inline float knob_table(const array<float, 5>& values, float x) noexcept {
    x = std::clamp(x, 0.0f, 1.0f);
    size_t i = 0;
    while (i + 2 < KNOB_AT.size() && x > KNOB_AT[i + 1]) {
        ++i;
    }
    const float t = (x - KNOB_AT[i]) / (KNOB_AT[i + 1] - KNOB_AT[i]);
    return values[i] + (values[i + 1] - values[i]) * t;
}

// The same, interpolating the logarithm (for decay times).
inline float knob_table_log(const array<float, 5>& values, float x) noexcept {
    array<float, 5> logs{};
    for (size_t i = 0; i < logs.size(); ++i) {
        logs[i] = std::log(values[i]);
    }
    return std::exp(knob_table(logs, x));
}

inline float db_to_gain(float db) noexcept {
    return std::pow(10.0f, db / 20.0f);
}

// E.Level: the wet's gain re 12:00 for the knob's turn (0–1). From 12:00 up the knob's
// square (3:00 +8.2 dB, 5:00 +12.0); below it a gentler power, through the 9:00 the focused
// session measured in five modes (13.5 dB under 12:00, where the square says 15.9).
constexpr float LEVEL_LOW_POWER = 1.696f;

inline float level_gain(float knob) noexcept {
    const float x = std::clamp(knob, 0.0f, 1.0f) / 0.5f;
    return x >= 1.0f ? x * x : std::pow(x, LEVEL_LOW_POWER);
}

// --- The feedback delay network (Room, Hall, Plate, Modulate) ---

constexpr size_t MAX_LINES = 16;
constexpr size_t MAX_TAPS = 24;
constexpr size_t MAX_EARLY = 16;

struct Tap
{
    float ms = 0.0f;
    float gain = 0.0f;
};

struct EarlyTap
{
    float ms = 0.0f;
    float gain_a = 0.0f;
    float gain_b = 0.0f;
};

struct TankShape
{
    size_t lines = MAX_LINES;
    array<float, MAX_LINES> line_ms{};
    array<float, 4> diffuser_ms{}; // input all-passes (0: unused)
    float diffusion = 0.0f;
    size_t taps = 1; // into the tank: the pre-delay, or a swell of echoes
    array<Tap, MAX_TAPS> tap{};
    size_t early = 0; // straight to the outputs: discrete first echoes
    array<EarlyTap, MAX_EARLY> early_tap{};
    float early_diffusion = 0.0f; // smears the first echoes into bursts (0: clean taps)
    float b_delay_ms = 0.0f; // output B later than A
    float low_hz = 250.0f; // below it the RT60 is low_ratio × the 1 kHz one
    float low_ratio = 1.0f;
    float high_hz = 3000.0f; // above it, high_ratio ×
    float high_ratio = 1.0f;
    float mod_hz = 0.0f; // the lines' modulation (0: a static tank)
    float mod_ms = 0.0f; // peak, per line
    float mid_hz = 250.0f; // a band whose RT60 is mid_ratio × the 1 kHz one (1: none)
    float mid_q = 1.0f;
    float mid_ratio = 1.0f;
};

// First-order shelf (bilinear), one sample at a time.
struct Shelf
{
    float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f;
    float x1 = 0.0f, y1 = 0.0f;

    // Gain `low` at DC, 1 at Nyquist; `k` = tan(π f / fs) at the crossover.
    void design_low(float low, float k) noexcept {
        const float norm = 1.0f / (1.0f + k);
        b0 = (1.0f + low * k) * norm;
        b1 = (low * k - 1.0f) * norm;
        a1 = (k - 1.0f) * norm;
    }
    // Gain 1 at DC, `high` at Nyquist.
    void design_high(float high, float k) noexcept {
        const float norm = 1.0f / (1.0f + k);
        b0 = (high + k) * norm;
        b1 = (k - high) * norm;
        a1 = (k - 1.0f) * norm;
    }
    float process(float x) noexcept {
        const float y = b0 * x + b1 * x1 - a1 * y1;
        x1 = x;
        y1 = y;
        return y;
    }
    void clear() noexcept {
        x1 = y1 = 0.0f;
    }
};

// Second-order peaking filter (Audio EQ Cookbook), one sample at a time.
struct Peak
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

    // `gain_db` at the centre; `w` = 2π f / fs.
    void design(float gain_db, float w, float q) noexcept {
        const float amp = std::pow(10.0f, gain_db / 40.0f);
        const float alpha = std::sin(w) / (2.0f * q);
        const float c = std::cos(w);
        const float a0 = 1.0f + alpha / amp;
        b0 = (1.0f + alpha * amp) / a0;
        b1 = -2.0f * c / a0;
        b2 = (1.0f - alpha * amp) / a0;
        a1 = -2.0f * c / a0;
        a2 = (1.0f - alpha / amp) / a0;
    }
    float process(float x) noexcept {
        const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
    void clear() noexcept {
        x1 = x2 = y1 = y2 = 0.0f;
    }
};

class Tank
{
public:
    void prepare(double sr, const TankShape& new_shape) {
        rate = static_cast<float>(sr);
        shape = new_shape;
        const float ms = rate / 1000.0f;
        depth = shape.mod_ms * ms;
        for (size_t i = 0; i < shape.lines; ++i) {
            length[i] = std::max(2.0f, shape.line_ms[i] * ms);
            lines[i].resize(static_cast<size_t>(length[i] + depth) + 4);
            lfo_offset[i] = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) /
                            static_cast<float>(shape.lines);
        }
        lfo_step = 2.0f * std::numbers::pi_v<float> * shape.mod_hz / rate;
        for (size_t i = 0; i < diffusers.size(); ++i) {
            diffusers[i].prepare(static_cast<size_t>(std::max(1.0f, shape.diffuser_ms[i] * ms)));
        }
        float longest = 1.0f;
        for (size_t i = 0; i < shape.taps; ++i) {
            tap_delay[i] = static_cast<size_t>(shape.tap[i].ms * ms);
            longest = std::max(longest, shape.tap[i].ms);
        }
        for (size_t i = 0; i < shape.early; ++i) {
            early_delay[i] = static_cast<size_t>(shape.early_tap[i].ms * ms);
            longest = std::max(longest, shape.early_tap[i].ms);
        }
        input.resize(static_cast<size_t>(longest * ms) + 2);
        for (size_t ch = 0; ch < 2; ++ch) {
            for (size_t i = 0; i < early_smear[ch].size(); ++i) {
                early_smear[ch][i].prepare(
                    static_cast<size_t>(EARLY_SMEAR_MS[i] * (ch == 0 ? 1.0f : 1.13f) * ms));
            }
        }
        b_delay = static_cast<size_t>(shape.b_delay_ms * ms);
        b_line.resize(b_delay + 2);
        norm = 1.0f / std::sqrt(static_cast<float>(shape.lines));
        reset();
    }

    void reset() noexcept {
        for (size_t i = 0; i < shape.lines; ++i) {
            lines[i].clear();
            low[i].clear();
            high[i].clear();
            mid[i].clear();
        }
        for (auto& d : diffusers) {
            d.clear();
        }
        for (auto& side : early_smear) {
            for (auto& d : side) {
                d.clear();
            }
        }
        input.clear();
        b_line.clear();
        lfo_phase = 0.0f;
    }

    // The loss per line for an RT60 of `rt60` seconds at 1 kHz, with the shape's ratios
    // below low_hz, above high_hz and (mid_ratio ≠ 1) around mid_hz.
    void set_rt60(float rt60) noexcept {
        const float k_low = std::tan(std::numbers::pi_v<float> * shape.low_hz / rate);
        const float k_high = std::tan(std::numbers::pi_v<float> * shape.high_hz / rate);
        const float w_mid = 2.0f * std::numbers::pi_v<float> * shape.mid_hz / rate;
        for (size_t i = 0; i < shape.lines; ++i) {
            const float seconds = length[i] / rate;
            gain[i] = std::pow(10.0f, -3.0f * seconds / rt60);
            const float g_low = std::pow(10.0f, -3.0f * seconds / (rt60 * shape.low_ratio));
            const float g_high = std::pow(10.0f, -3.0f * seconds / (rt60 * shape.high_ratio));
            low[i].design_low(g_low / gain[i], k_low);
            high[i].design_high(g_high / gain[i], k_high);
            if (shape.mid_ratio != 1.0f) {
                // The extra loss there per pass, in dB (60 dB per RT60 of delay).
                const float extra_db =
                    -60.0f * seconds * (1.0f / (rt60 * shape.mid_ratio) - 1.0f / rt60);
                mid[i].design(extra_db, w_mid, shape.mid_q);
            }
        }
    }

    void process(float x, float& a, float& b) noexcept {
        input.push(x);
        float into = 0.0f;
        for (size_t i = 0; i < shape.taps; ++i) {
            into += shape.tap[i].gain * input.at(tap_delay[i]);
        }
        float early_a = 0.0f, early_b = 0.0f;
        for (size_t i = 0; i < shape.early; ++i) {
            const float v = input.at(early_delay[i]);
            early_a += shape.early_tap[i].gain_a * v;
            early_b += shape.early_tap[i].gain_b * v;
        }
        if (shape.early_diffusion > 0.0f) {
            for (size_t i = 0; i < EARLY_SMEAR_MS.size(); ++i) {
                early_a = early_smear[0][i].process(early_a, shape.early_diffusion);
                early_b = early_smear[1][i].process(early_b, shape.early_diffusion);
            }
        }
        for (size_t i = 0; i < diffusers.size(); ++i) {
            if (shape.diffuser_ms[i] > 0.0f) {
                into = diffusers[i].process(into, shape.diffusion);
            }
        }

        array<float, MAX_LINES> y{};
        if (depth > 0.0f) {
            lfo_phase += lfo_step;
            if (lfo_phase > 2.0f * std::numbers::pi_v<float>) {
                lfo_phase -= 2.0f * std::numbers::pi_v<float>;
            }
        }
        for (size_t i = 0; i < shape.lines; ++i) {
            float v = 0.0f;
            if (depth > 0.0f) {
                const float phase = lfo_phase + lfo_offset[i];
                const float read = length[i] + depth * std::sin(phase);
                v = lines[i].interpolate(read - 1.0f);
            } else {
                v = lines[i].at(static_cast<size_t>(length[i]) - 1);
            }
            y[i] = low[i].process(high[i].process(gain[i] * v));
            if (shape.mid_ratio != 1.0f) {
                y[i] = mid[i].process(y[i]);
            }
        }
        float sum_a = 0.0f, sum_b = 0.0f;
        for (size_t i = 0; i < shape.lines; ++i) {
            sum_a += i % 2 == 0 ? y[i] : -y[i];
            sum_b += (i / 2) % 2 == 0 ? y[i] : -y[i];
        }
        hadamard(y, shape.lines);
        for (size_t i = 0; i < shape.lines; ++i) {
            lines[i].push(y[i] + INJECT[i] * into * INPUT_GAIN);
        }
        a = norm * sum_a + early_a;
        b_line.push(norm * sum_b + early_b);
        b = b_line.at(b_delay);
    }

private:
    static constexpr float INPUT_GAIN = 0.5f;
    static constexpr array<float, 3> EARLY_SMEAR_MS{1.7f, 3.9f, 6.7f};
    // The input's signs per line: 6 of 16 (2 of 8) of it reaches each output on the first
    // pass, so the tail starts at once (a Hadamard row would cancel there and build slowly).
    static constexpr array<float, MAX_LINES> INJECT{1, 1, -1, -1, -1, -1, 1, -1,
                                                    1, 1, -1, -1, 1,  -1, 1, -1};

    static void hadamard(array<float, MAX_LINES>& v, size_t n) noexcept {
        for (size_t h = 1; h < n; h *= 2) {
            for (size_t i = 0; i < n; i += 2 * h) {
                for (size_t j = i; j < i + h; ++j) {
                    const float p = v[j], q = v[j + h];
                    v[j] = p + q;
                    v[j + h] = p - q;
                }
            }
        }
        const float scale = 1.0f / std::sqrt(static_cast<float>(n));
        for (size_t i = 0; i < n; ++i) {
            v[i] *= scale;
        }
    }

    float rate = 48000.0f;
    TankShape shape{};
    array<DelayLine, MAX_LINES> lines;
    array<float, MAX_LINES> length{}, gain{}, lfo_offset{};
    array<Shelf, MAX_LINES> low{}, high{};
    array<Peak, MAX_LINES> mid{};
    array<reverb_tanks::AllPass, 4> diffusers;
    array<array<reverb_tanks::AllPass, 3>, 2> early_smear;
    DelayLine input, b_line;
    array<size_t, MAX_TAPS> tap_delay{};
    array<size_t, MAX_EARLY> early_delay{};
    size_t b_delay = 0;
    float depth = 0.0f, lfo_phase = 0.0f, lfo_step = 0.0f, norm = 1.0f;
};

// --- The spring ---

constexpr size_t MAX_SPRINGS = 3;

struct SpringShape
{
    size_t springs = MAX_SPRINGS;
    array<float, MAX_SPRINGS> loop_ms{}; // each loop's plain delay (the chirps add to it)
    array<float, MAX_SPRINGS> to_a{}; // each spring's share of the outputs
    array<float, MAX_SPRINGS> to_b{};
    size_t stages = 24; // stretched all-passes per loop
    float transition_hz = 3430.0f; // where their group delay peaks: fs / (2 K)
    float allpass = 0.6f; // their coefficient: how sharp the peak is
    float low_cut_hz = 100.0f; // first-order, in each loop: the band edges' own decay
    float high_cut_hz = 5000.0f;
    float pre_delay_ms = 0.0f;
};

class Spring
{
public:
    void prepare(double sr, const SpringShape& new_shape) {
        rate = static_cast<float>(sr);
        shape = new_shape;
        const float ms = rate / 1000.0f;
        stretch = std::max<size_t>(
            1, static_cast<size_t>(std::lround(rate / (2.0f * shape.transition_hz))));
        for (size_t s = 0; s < shape.springs; ++s) {
            loop[s].resize(static_cast<size_t>(shape.loop_ms[s] * ms) + 2);
            history_x[s].assign(shape.stages * stretch, 0.0f);
            history_y[s].assign(shape.stages * stretch, 0.0f);
        }
        pre.resize(static_cast<size_t>(shape.pre_delay_ms * ms) + 2);
        hp_a = std::exp(-2.0f * std::numbers::pi_v<float> * shape.low_cut_hz / rate);
        lp_k = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * shape.high_cut_hz / rate);
        reset();
    }

    void reset() noexcept {
        for (size_t s = 0; s < shape.springs; ++s) {
            loop[s].clear();
            std::fill(history_x[s].begin(), history_x[s].end(), 0.0f);
            std::fill(history_y[s].begin(), history_y[s].end(), 0.0f);
            feedback[s] = hp_x[s] = hp_y[s] = lp[s] = 0.0f;
        }
        pre.clear();
        index = 0;
    }

    // Loop gains for an RT60 of `rt60` seconds at 1 kHz.
    void set_rt60(float rt60) noexcept {
        const float w = 2.0f * std::numbers::pi_v<float> * 1000.0f / rate;
        const float kw = static_cast<float>(stretch) * w;
        const float a = shape.allpass;
        const float stage_delay =
            static_cast<float>(stretch) * (1.0f - a * a) / (1.0f + 2.0f * a * std::cos(kw) + a * a);
        // |H| of the loop's one-pole low-pass and high-pass at 1 kHz.
        const float c = std::cos(w);
        const float lp_mag =
            lp_k / std::sqrt(1.0f - 2.0f * (1.0f - lp_k) * c + (1.0f - lp_k) * (1.0f - lp_k));
        const float hp_mag =
            hp_a * std::sqrt(2.0f - 2.0f * c) / std::sqrt(1.0f - 2.0f * hp_a * c + hp_a * hp_a);
        for (size_t s = 0; s < shape.springs; ++s) {
            const float seconds =
                shape.loop_ms[s] / 1000.0f + static_cast<float>(shape.stages) * stage_delay / rate;
            gain[s] = std::min(0.999f, std::pow(10.0f, -3.0f * seconds / rt60) / (lp_mag * hp_mag));
        }
    }

    void process(float x, float& a, float& b) noexcept {
        pre.push(x);
        const float in = pre.at(pre.size() - 2);
        a = b = 0.0f;
        const float k = shape.allpass;
        for (size_t s = 0; s < shape.springs; ++s) {
            float v = in + feedback[s];
            float* hx = history_x[s].data();
            float* hy = history_y[s].data();
            for (size_t m = 0; m < shape.stages; ++m) {
                const size_t at = m * stretch + index;
                const float y = k * v + hx[at] - k * hy[at];
                hx[at] = v;
                hy[at] = y;
                v = y;
            }
            loop[s].push(v);
            const float w = loop[s].oldest();
            // The loop's band: a one-pole high-pass and low-pass.
            const float h = hp_a * (hp_y[s] + w - hp_x[s]);
            hp_x[s] = w;
            hp_y[s] = h;
            lp[s] += lp_k * (h - lp[s]);
            feedback[s] = gain[s] * lp[s];
            a += shape.to_a[s] * w;
            b += shape.to_b[s] * w;
        }
        index = index + 1 == stretch ? 0 : index + 1;
    }

private:
    float rate = 48000.0f;
    SpringShape shape{};
    size_t stretch = 1, index = 0;
    array<DelayLine, MAX_SPRINGS> loop;
    array<std::vector<float>, MAX_SPRINGS> history_x, history_y;
    array<float, MAX_SPRINGS> gain{}, feedback{}, hp_x{}, hp_y{}, lp{};
    DelayLine pre;
    float hp_a = 0.0f, lp_k = 1.0f;
};

// --- The voicings (fitted: tools/capture/fit_rv6.py) ---

struct Voicing
{
    array<float, 5> rt60_1k{}; // the tank's RT60 at 1 kHz at KNOB_AT, s
    array<float, 5> energy_db{}; // the wet's level correction at KNOB_AT (Time)
    float gain_db = 0.0f; // the wet at Level 12:00, Time 12:00
    FilterChain<4> eq{}; // the mode's colour, re 1 kHz
};

// Tone: the filter on the wet, the same in every mode (the focused session: the four
// time-invariant modes agree within 0.1 dB), three sections at each measured position —
// KNOB_AT; 12:00 is flat. Below noon a treble cut from 1 kHz with a deep notch at 4 kHz and
// nothing under 500 Hz; above noon a low cut under 150 Hz and a treble lift (3:00: a bump at
// 1.2 kHz before the shelf). Between two measured positions on the same side of noon the
// sections' frequency (its logarithm), Q and gain are interpolated; towards noon the nearest
// position's gains fade to 0 dB.
struct ToneShape
{
    array<FilterChain<3>, 5> at{};
};

// Tone below noon in Plate, Hall and Room: one low reverb, the same on both outputs, beside
// the mode's own decorrelated one (the focused session: under 250 Hz the two outputs go from
// unlike to alike, and Plate, whose own wet has little bass, gains 19 dB at 80 Hz). It starts
// ~40 ms after the note whatever the mode, has the same level and colour in the three (even
// per third octave from 50 to 400 Hz, falling above), a decay of its own that follows Time
// (shorter than Plate's and Hall's tanks, longer than Room's), and doesn't follow E.Level
// between 12:00 and 3:00 (the only settings measured: here it follows the knob below 12:00,
// so that 7:00 is silence, and stays put above). None in Modulate and Spring.
// Here: a tank of its own (LOW_TANK), one output of it through `band`.
struct LowShape
{
    FilterChain<2> band{};
    array<float, MODE_COUNT> gain_db{}; // re the dry at E.Level and Time 12:00 (NO_LOW: none)
    array<float, 5> at{}; // its level at KNOB_AT (Tone), re gain_db
    array<array<float, 5>, MODE_COUNT> rt60{}; // per mode, its RT60 at KNOB_AT (Time), s
    array<array<float, 5>, MODE_COUNT> time_db{}; // per mode, its level at KNOB_AT (Time)
};
constexpr float NO_LOW = -200.0f;

// The low reverb's tank: a 20 ms tap and a dense set of lines (its first sound at ~40 ms),
// the same decay from 50 to 500 Hz, the top dying sooner.
constexpr TankShape LOW_TANK{16,
                             {19.1f, 21.7f, 24.1f, 26.9f, 29.3f, 32.3f, 35.1f, 37.9f, 41.3f, 44.1f,
                              47.9f, 51.1f, 54.7f, 58.3f, 62.3f, 66.1f},
                             {3.1f, 5.3f, 8.9f, 12.1f},
                             0.6f,
                             1,
                             {{{20.0f, 1.0f}}},
                             0,
                             {},
                             0.0f,
                             0.0f,
                             60.0f,
                             1.0f,
                             2000.0f,
                             0.6f,
                             0.0f,
                             0.0f};

// Per mode, in Mode order. Spring and Shimmer have their own engines.
constexpr array<TankShape, MODE_COUNT> TANKS{{
    // Modulate: first echoes at 27 and 35 ms, a dense tail; one LFO on every line.
    {16,
     {31.3f, 34.7f, 38.9f, 42.1f, 45.7f, 49.3f, 53.9f, 57.1f, 61.7f, 65.3f, 69.1f, 73.7f, 77.9f,
      82.3f, 87.1f, 91.9f},
     {5.3f, 8.9f, 13.7f, 19.1f},
     0.65f,
     1,
     {{{9.0f, 1.0f}}},
     2,
     {{{27.0f, 0.04f, 0.03f}, {35.0f, 0.03f, 0.04f}}},
     0.0f,
     0.7f,
     350.0f,
     0.88f,
     2500.0f,
     0.75f,
     1.35f,
     0.2f},
    {}, // Spring
    // Plate: a pre-delay, heavy diffusion, a flat decay.
    {16,
     {17.3f, 19.9f, 22.1f, 24.7f, 27.3f, 29.9f, 32.9f, 35.3f, 38.3f, 41.1f, 44.3f, 47.3f, 50.9f,
      54.1f, 57.7f, 61.3f},
     {3.1f, 5.3f, 8.9f, 12.1f},
     0.7f,
     1,
     {{{41.0f, 1.0f}}},
     0,
     {},
     0.0f,
     5.0f,
     300.0f,
     1.05f,
     4000.0f,
     0.87f,
     0.0f,
     0.0f},
    // Hall: long lines; its slow, sparse swell is the measured early response (Rv6EarlyData.h).
    // The tank is fed 240 ms in, after the swell: the pedal's swell is the same at every Time
    // (but for its level) for its first ~250 ms and only then decays as Time says, so its
    // tank comes after it (the focused session; fed at 26 ms the tank's own start, decaying
    // from the first moment, came through 7 dB loud at short Time).
    {16,
     {47.3f, 53.9f, 59.3f, 64.1f, 70.9f, 76.3f, 83.1f, 89.9f, 96.1f, 103.3f, 110.9f, 118.3f, 126.1f,
      134.9f, 142.3f, 151.7f},
     {2.3f, 0.0f, 0.0f, 0.0f},
     0.5f,
     1,
     {{{240.0f, 1.0f}}},
     0,
     {},
     0.6f,
     0.0f,
     500.0f,
     1.33f,
     1500.0f,
     0.77f,
     0.0f,
     0.0f},
    // Room: a pre-delay, short lines, a quick dense build-up.
    {16,
     {10.7f, 12.1f, 13.9f, 15.7f, 17.3f, 19.1f, 21.1f, 22.9f, 25.1f, 27.1f, 29.3f, 31.7f, 34.1f,
      36.7f, 39.1f, 41.9f},
     {1.3f, 2.1f, 3.1f, 4.3f},
     0.6f,
     1,
     {{{47.0f, 1.0f}}},
     0,
     {},
     0.0f,
     3.0f,
     300.0f,
     1.0f,
     3000.0f,
     0.8f,
     0.0f,
     0.0f},
    {}, // Shimmer uses its measured model.
}};

// Output A's main chirp arrives at ~90 ms, B's at ~97 ms after a weak one at ~75 ms (the
// first arrivals' peak times at 1 kHz); the three loops' repeats are near the envelope's
// 24, 42 and 47 ms.
constexpr SpringShape SPRING{3,
                             {40.4f, 22.5f, 46.4f},
                             {1.0f, 0.3f, 0.2f},
                             {0.2f, 0.5f, 1.0f},
                             24,
                             3430.0f,
                             0.6f,
                             140.0f,
                             6500.0f,
                             50.0f};

// BEGIN FITTED (tools/capture/fit_rv6.py --write rewrites this block)
constexpr array<Voicing, MODE_COUNT> VOICINGS{{
    {{0.8508f, 1.011f, 5.749f, 10.36f, 15.03f},
     {0.8929f, 0.1644f, 0.0f, 0.3606f, 0.6724f},
     -8.74f,
     {{{FilterKind::HIGH_PASS, 68.37f, 0.7f, 0.0f},
       {FilterKind::LOW_SHELF, 210.0f, 0.7f, 1.907f},
       {FilterKind::PEAK, 702.4f, 1.0f, 3.363f},
       {FilterKind::HIGH_SHELF, 7734.1f, 0.7f, -6.511f}}}},
    {{1.178f, 1.267f, 2.787f, 5.288f, 7.028f},
     {-0.4068f, -0.3168f, 0.0f, -0.4965f, -0.984f},
     -22.37f,
     {{{FilterKind::HIGH_PASS, 20.0f, 0.5412f, 0.0f},
       {FilterKind::HIGH_PASS, 208.2f, 1.307f, 0.0f},
       {FilterKind::LOW_PASS, 16000.0f, 0.9f, 0.0f},
       {FilterKind::PEAK, 1482.7f, 1.0f, -3.336f}}}},
    {{0.6896f, 0.736f, 2.964f, 7.189f, 11.15f},
     {0.8827f, 0.4716f, 0.0f, -0.4554f, -0.43f},
     -14.82f,
     {{{FilterKind::HIGH_PASS, 34.54f, 0.7f, 0.0f},
       {FilterKind::LOW_SHELF, 154.4f, 0.7f, -13.51f},
       {FilterKind::PEAK, 1328.7f, 1.0f, 5.401f},
       {FilterKind::HIGH_SHELF, 7685.9f, 0.7f, 1.434f}}}},
    {{0.4932f, 0.5508f, 3.832f, 7.028f, 10.04f},
     {-0.7549f, -0.834f, 0.0f, 1.009f, 1.023f},
     -15.35f,
     {{{FilterKind::HIGH_PASS, 20.0f, 0.7f, 0.0f},
       {FilterKind::LOW_SHELF, 1000.0f, 0.7f, 4.55f},
       {FilterKind::PEAK, 1530.4f, 1.0f, 5.833f},
       {FilterKind::HIGH_SHELF, 7122.9f, 0.7f, -2.908f}}}},
    {{0.3704f, 0.3952f, 1.256f, 3.392f, 5.226f},
     {3.072f, 3.017f, 0.0f, -0.2126f, -0.245f},
     -9.68f,
     {{{FilterKind::HIGH_PASS, 221.8f, 0.7f, 0.0f},
       {FilterKind::LOW_SHELF, 350.1f, 0.7f, 13.5f},
       {FilterKind::PEAK, 1350.6f, 1.0f, 4.115f},
       {FilterKind::HIGH_SHELF, 9112.1f, 0.7f, -6.258f}}}},
    {{4.514f, 5.874f, 11.73f, 19.7f, 22.37f},
     {-4.291f, -4.745f, 0.0f, 2.75f, 3.242f},
     -0.9251f,
     {{}}},
}};
// END FITTED

// BEGIN FITTED TONE (tools/capture/rv6/tone.py --write rewrites this block)
constexpr ToneShape TONE{{{
    {{{FilterKind::HIGH_SHELF, 1413.0f, 0.7542f, -6.782f},
      {FilterKind::PEAK, 4015.1f, 5.025f, -12.3f},
      {FilterKind::PEAK, 3756.7f, 0.905f, -2.67f}}},
    {{{FilterKind::HIGH_SHELF, 2673.0f, 0.7504f, -4.586f},
      {FilterKind::PEAK, 4011.1f, 3.339f, -6.307f},
      {FilterKind::PEAK, 4233.9f, 1.046f, -1.729f}}},
    {{}},
    {{{FilterKind::LOW_SHELF, 94.012f, 0.7121f, -5.93f},
      {FilterKind::PEAK, 1256.6f, 1.636f, 2.229f},
      {FilterKind::HIGH_SHELF, 4439.9f, 0.6887f, 8.394f}}},
    {{{FilterKind::LOW_SHELF, 93.837f, 0.7084f, -5.949f},
      {FilterKind::PEAK, 1010.0f, 0.9493f, -0.4692f},
      {FilterKind::HIGH_SHELF, 1871.4f, 0.5922f, 9.87f}}},
}}};

constexpr LowShape LOW{{{{FilterKind::LOW_SHELF, 292.19f, 0.4224f, 23.34f},
                         {FilterKind::LOW_PASS_1, 1549.3f, 0.7071f, 0.0f}}},
                       {NO_LOW, NO_LOW, -22.67f, -25.6f, -21.77f, NO_LOW},
                       {1.0f, 0.8713f, 0.0f, 0.0f, 0.0f},
                       {{{},
                         {},
                         {0.471f, 0.504f, 2.09f, 4.12f, 5.76f},
                         {0.518f, 0.572f, 3.32f, 5.38f, 7.14f},
                         {0.502f, 0.531f, 1.47f, 3.11f, 4.31f},
                         {}}},
                       {{{},
                         {},
                         {2.68f, 2.68f, 0.0f, -1.53f, -3.05f},
                         {3.53f, 3.53f, 0.0f, -0.8f, -1.6f},
                         {2.78f, 2.78f, 0.0f, -1.98f, -3.97f},
                         {}}}};
// END FITTED TONE

// The tails' finer shape, per mode (Room, Hall, Modulate; fit_rv6_decay.py fits it to
// the pedal's octave bands at every Time position): the pedal's decay bends — each 10 dB takes
// longer than the last, 20–35 % longer by 50 dB down — where one tank's is straight; a second,
// slower tank (lines × 1.13) under the first makes the bend (Modulate has no use for one;
// Plate's traded its short Time settings for it: its measured start takes the tanks off as
// they are at noon).
// Also the tank's own RT ratios re TANKS, and a band of its own (Room's and Modulate's lows
// die sooner around 200 Hz than at 125 and 500).
struct DecayShape
{
    float low_scale = 1.0f; // the tank's low_ratio, re TANKS
    float high_scale = 1.0f; // its high_ratio
    float mid_hz = 250.0f; // its band (TankShape's mid_*; ratio 1: none)
    float mid_q = 1.0f;
    float mid_ratio = 1.0f;
    float slow_rt = 1.0f; // the slower tank's RT60, re the tank's
    float slow_gain = 0.0f; // its level (0: none)
    float slow_low_scale = 1.0f; // its low and high ratios, re the tank's
    float slow_high_scale = 1.0f;
    array<float, 5> slow_at{1.0f, 1.0f, 1.0f, 1.0f,
                            1.0f}; // its level at KNOB_AT (Time), re slow_gain
};

// BEGIN FITTED DECAY (tools/capture/fit_rv6_decay.py --write rewrites this block)
constexpr array<DecayShape, MODE_COUNT> DECAYS{
    {DecayShape{1.124f, 1.082f, 202.3f, 1.5f, 0.7185f, 1.0f, 0.0f, 1.0f, 1.0f}, DecayShape{},
     DecayShape{},
     DecayShape{1.018f,
                0.892f,
                100.0f,
                1.5f,
                0.8574f,
                1.387f,
                0.7372f,
                0.5584f,
                0.5f,
                {0.6866f, 0.819f, 1.0f, 1.0f, 1.0f}},
     DecayShape{1.169f,
                0.918f,
                199.3f,
                1.5f,
                0.5789f,
                1.158f,
                0.1102f,
                1.962f,
                0.5034f,
                {0.4486f, 0.4346f, 1.0f, 1.0f, 1.0f}},
     DecayShape{}}};
// END FITTED DECAY

// --- The engine: one mode at a time, the pedal's frame around it ---

class Engine
{
public:
    void prepare(double sr) {
        rate = static_cast<float>(sr);
        spring.prepare(sr, spring_shape);
        for (size_t m = 0; m < MODE_COUNT; ++m) {
            prepare_mode(m);
        }
        shimmer.prepare(sr);
        low_tank.prepare(sr, LOW_TANK);
        low_band.design(low.band, rate);
        smooth_k = 1.0f - std::exp(-1.0f / (0.02f * rate));
        configured = false;
        reset();
    }

    void reset() noexcept {
        for (auto& t : tanks) {
            t.reset();
        }
        for (auto& t : slow_tanks) {
            t.reset();
        }
        for (auto& e : early) {
            e.reset();
        }
        spring.reset();
        if (mode == Mode::SHIMMER) {
            shimmer.reset();
        }
        for (auto& s : eq_state) {
            s.reset();
        }
        for (auto& s : tone_state) {
            s.reset();
        }
        low_tank.reset();
        low_state.reset();
        low_idle = true;
        level = target_level;
        low_level = low_target;
    }

    // Another Time law for a mode: its RT60s and energy corrections, and its measured start's
    // levels and where its extra decay starts (fitting them: tools/capture/render_rv6.cpp).
    // Call it with the audio stopped.
    void set_time_law(Mode m, const array<float, 5>& rt60_1k, const array<float, 5>& energy_db,
                      const array<float, 5>& early_db, float early_from_ms) noexcept {
        voicings[static_cast<size_t>(m)].rt60_1k = rt60_1k;
        voicings[static_cast<size_t>(m)].energy_db = energy_db;
        early_gain_db[static_cast<size_t>(m)] = early_db;
        early_from[static_cast<size_t>(m)] = early_from_ms;
        early[static_cast<size_t>(m)].set_decay_from(early_from_ms);
        configured = false;
        last_time = -1.0f;
    }

    // Another low reverb for Tone below noon (fitting it: tools/capture/render_rv6.cpp). Call
    // it with the audio stopped.
    void set_low_shape(const LowShape& shape) noexcept {
        low = shape;
        low_band.design(low.band, rate);
        configured = false;
        last_time = -1.0f;
    }

    // Another spring (fitting it: tools/capture/render_rv6.cpp). Allocates: call it with the
    // audio stopped, then reset().
    void set_spring_shape(const SpringShape& shape) {
        spring_shape = shape;
        spring.prepare(rate, spring_shape);
        prepare_mode(static_cast<size_t>(Mode::SPRING));
        configured = false;
    }

    // Another decay shape for a mode (fitting it: tools/capture/render_rv6.cpp). Allocates:
    // call it with the audio stopped, then reset().
    void set_decay_shape(Mode m, const DecayShape& shape) {
        decays[static_cast<size_t>(m)] = shape;
        prepare_mode(static_cast<size_t>(m));
        configured = false;
    }

    // Without the measured early responses: the tanks alone (tools/capture/rv6_early.py
    // measures them so). Call it with the audio stopped.
    void set_early_enabled(bool enabled) noexcept {
        early_enabled = enabled;
    }

    // Per block (audio thread): the mode and the three knobs, 0–1 of their turn.
    void set_controls(Mode new_mode, float time, float tone, float level_knob) noexcept {
        if (new_mode == Mode::SHIMMER) {
            shimmer.set_controls(time, tone);
            target_level = level_gain(level_knob);
            if (!configured || mode != new_mode) {
                shimmer.reset();
                level = target_level;
            }
            configured = true;
            mode = new_mode;
            return;
        }
        const auto m = static_cast<size_t>(new_mode);
        if (!configured || new_mode != mode) {
            mode = new_mode;
            if (mode == Mode::SPRING) {
                spring.reset();
            } else {
                tanks[m].reset();
                slow_tanks[m].reset();
            }
            early[m].reset();
            for (auto& s : eq_state) {
                s.reset();
            }
            low_tank.reset();
            low_state.reset();
            low_idle = true;
            last_time = -1.0f;
            last_tone = -1.0f; // Tone means something else in Shimmer
        }
        const Voicing& v = voicings[m];
        if (time != last_time) {
            last_time = time;
            const float rt = knob_table_log(v.rt60_1k, time);
            if (mode == Mode::SPRING) {
                spring.set_rt60(rt);
            } else {
                tanks[m].set_rt60(rt);
                if (decays[m].slow_gain > 0.0f) {
                    slow_tanks[m].set_rt60(rt * decays[m].slow_rt);
                }
                slow_level = decays[m].slow_gain * knob_table(decays[m].slow_at, time);
            }
            // A longer tail starts quieter: the wet's energy held (∝ RT60 at a fixed input).
            time_gain = std::sqrt(v.rt60_1k[2] / rt) * db_to_gain(knob_table(v.energy_db, time));
            // The early response was measured at noon: the tank's decay at this Time re noon's
            // (3 ln 10 nepers per RT60).
            constexpr float NEPERS_PER_RT60 = 6.9078f;
            early[m].set_time(NEPERS_PER_RT60 * (1.0f / rt - 1.0f / v.rt60_1k[2]),
                              db_to_gain(knob_table(early_gain_db[m], time)));
            if (low.gain_db[m] > NO_LOW) {
                // The low reverb's own decay; its level as the tanks': a longer tail starts
                // quieter, and a correction per position.
                const float low_rt = knob_table_log(low.rt60[m], time);
                low_tank.set_rt60(low_rt);
                low_time_gain = std::sqrt(low.rt60[m][2] / low_rt) *
                                db_to_gain(knob_table(low.time_db[m], time));
            }
        }
        if (!configured || tone != last_tone) {
            last_tone = tone;
            design_tone(tone);
        }
        const float wet = level_gain(level_knob);
        target_level = wet * db_to_gain(v.gain_db) * time_gain;
        // The low reverb of Tone below noon: its own level, the tank's Time gain.
        low_on = mode != Mode::SPRING && low.gain_db[m] > NO_LOW;
        low_target = low_on ? db_to_gain(low.gain_db[m]) * knob_table(low.at, tone) *
                                  low_time_gain * std::min(wet, 1.0f)
                            : 0.0f;
        if (!configured) {
            level = target_level;
            low_level = low_target;
        }
        configured = true;
    }

    // One sample: the wet of A and B (the caller adds the dry).
    void process(float x, float& a, float& b) noexcept {
        const auto m = static_cast<size_t>(mode);
        if (mode == Mode::SPRING) {
            spring.process(x, a, b);
        } else if (mode == Mode::SHIMMER) {
            shimmer.process(x, a, b);
            level += smooth_k * (target_level - level);
            a *= level;
            b *= level;
            return;
        } else {
            tanks[m].process(x, a, b);
            const DecayShape& d = decays[m];
            if (d.slow_gain > 0.0f) {
                float slow_a = 0.0f, slow_b = 0.0f;
                slow_tanks[m].process(x, slow_a, slow_b);
                a += slow_level * slow_a;
                b += slow_level * slow_b;
            }
        }
        float early_a = 0.0f, early_b = 0.0f;
        if (early_enabled && !early[m].empty()) {
            early[m].process(x, early_a, early_b);
        }
        level += smooth_k * (target_level - level);
        a = level * tone_state[0].process(eq_state[0].process(a, eq[m]) + early_a, tone);
        b = level * tone_state[1].process(eq_state[1].process(b, eq[m]) + early_b, tone);
        if (low_on && (low_target > 0.0f || low_level > LOW_SILENT)) {
            low_idle = false;
            low_level += smooth_k * (low_target - low_level);
            float low_a = 0.0f, low_b = 0.0f;
            low_tank.process(x, low_a, low_b);
            const float both = low_level * low_state.process(low_a, low_band);
            a += both;
            b += both;
        } else if (!low_idle) {
            // Tone back at noon or above: the low reverb rests, emptied for the next time.
            low_tank.reset();
            low_state.reset();
            low_level = 0.0f;
            low_idle = true;
        }
    }

private:
    // The tank's (or the spring's) impulse response at noon as process() has it before the
    // level: through the mode's EQ (Tone is flat at noon). Leaves it reset.
    void tank_response(size_t m, size_t length, std::vector<float>& a, std::vector<float>& b) {
        a.assign(length, 0.0f);
        b.assign(length, 0.0f);
        if (static_cast<Mode>(m) == Mode::SPRING) {
            spring.reset();
            spring.set_rt60(VOICINGS[m].rt60_1k[2]);
            array<BiquadChainState<4>, 2> state{};
            for (size_t i = 0; i < length; ++i) {
                float out_a = 0.0f, out_b = 0.0f;
                spring.process(i == 0 ? 1.0f : 0.0f, out_a, out_b);
                a[i] = state[0].process(out_a, eq[m]);
                b[i] = state[1].process(out_b, eq[m]);
            }
            spring.reset();
            return;
        }
        const DecayShape& d = decays[m];
        tanks[m].reset();
        tanks[m].set_rt60(VOICINGS[m].rt60_1k[2]);
        if (d.slow_gain > 0.0f) {
            slow_tanks[m].reset();
            slow_tanks[m].set_rt60(VOICINGS[m].rt60_1k[2] * d.slow_rt);
        }
        array<BiquadChainState<4>, 2> state{};
        for (size_t i = 0; i < length; ++i) {
            const float x = i == 0 ? 1.0f : 0.0f;
            float out_a = 0.0f, out_b = 0.0f;
            tanks[m].process(x, out_a, out_b);
            if (d.slow_gain > 0.0f) {
                float slow_a = 0.0f, slow_b = 0.0f;
                slow_tanks[m].process(x, slow_a, slow_b);
                out_a += d.slow_gain * d.slow_at[2] * slow_a;
                out_b += d.slow_gain * d.slow_at[2] * slow_b;
            }
            a[i] = state[0].process(out_a, eq[m]);
            b[i] = state[1].process(out_b, eq[m]);
        }
        tanks[m].reset();
        slow_tanks[m].reset();
    }

    // A mode's tanks (the slower one too), EQ and early response: at prepare() and when its
    // decay shape changes. Spring and Shimmer have their own engines.
    void prepare_mode(size_t m) {
        const auto which = static_cast<Mode>(m);
        if (which == Mode::SHIMMER) {
            return;
        }
        eq[m].design(VOICINGS[m].eq, rate);
        if (which != Mode::SPRING) {
            const DecayShape& d = decays[m];
            TankShape shape = TANKS[m];
            shape.low_ratio *= d.low_scale;
            shape.high_ratio *= d.high_scale;
            shape.mid_hz = d.mid_hz;
            shape.mid_q = d.mid_q;
            shape.mid_ratio = d.mid_ratio;
            tanks[m].prepare(rate, shape);
            if (d.slow_gain > 0.0f) {
                TankShape slow = shape;
                for (size_t i = 0; i < slow.lines; ++i) {
                    slow.line_ms[i] *= 1.13f;
                }
                slow.early = 0; // the first echoes come once, from the tank
                slow.low_ratio *= d.slow_low_scale;
                slow.high_ratio *= d.slow_high_scale;
                slow_tanks[m].prepare(rate, slow);
            }
        }
        const EarlyKernel& kernel = EARLY[m];
        std::vector<float> own_a, own_b;
        if (kernel.length > 0) {
            const auto seconds = static_cast<double>(kernel.offset + kernel.length) /
                                 static_cast<double>(kernel.rate);
            tank_response(m, static_cast<size_t>(std::ceil(seconds * rate)) + 64, own_a, own_b);
        }
        early[m].prepare(rate, kernel, db_to_gain(VOICINGS[m].gain_db), own_a, own_b);
        early[m].set_decay_from(early_from[m]);
    }

    // Tone's filter at `x` of the knob's turn (ToneShape).
    static array<array<float, 5>, MODE_COUNT> early_gains() noexcept {
        array<array<float, 5>, MODE_COUNT> out{};
        for (size_t m = 0; m < MODE_COUNT; ++m) {
            out[m] = EARLY[m].gain_db;
        }
        return out;
    }

    static array<float, MODE_COUNT> early_starts() noexcept {
        array<float, MODE_COUNT> out{};
        for (size_t m = 0; m < MODE_COUNT; ++m) {
            out[m] = EARLY[m].decay_from_ms;
        }
        return out;
    }

    void design_tone(float x) noexcept {
        x = std::clamp(x, 0.0f, 1.0f);
        // The two measured positions on this side of noon: `outer` nearer the knob's end.
        const size_t outer = x < 0.5f ? 0 : 4, inner = x < 0.5f ? 1 : 3;
        // 0 at `inner` to 1 at `outer`; 1 at `inner` to 0 at noon.
        const float towards_end =
            std::clamp((x - KNOB_AT[inner]) / (KNOB_AT[outer] - KNOB_AT[inner]), 0.0f, 1.0f);
        const float from_noon = std::clamp((x - 0.5f) / (KNOB_AT[inner] - 0.5f), 0.0f, 1.0f);
        tone.count = 0;
        for (size_t i = 0; i < TONE.at[inner].size(); ++i) {
            const FilterSection& near = TONE.at[inner][i];
            const FilterSection& far = TONE.at[outer][i];
            if (near.kind == FilterKind::NONE) {
                continue;
            }
            const FilterSection section{
                near.kind, near.hz * std::pow(far.hz / near.hz, towards_end),
                near.q + (far.q - near.q) * towards_end,
                from_noon * (near.gain_db + (far.gain_db - near.gain_db) * towards_end)};
            tone.sections[tone.count++].design(section, rate);
        }
        tone.gain = 1.0f; // the tone's own level is part of the measured curves
    }

    float rate = 48000.0f;
    Mode mode = Mode::PLATE;
    bool configured = false;
    array<Tank, MODE_COUNT> tanks;
    Spring spring;
    Shimmer shimmer;
    array<Voicing, MODE_COUNT> voicings = VOICINGS;
    array<array<float, 5>, MODE_COUNT> early_gain_db = early_gains();
    array<float, MODE_COUNT> early_from = early_starts();
    static constexpr float LOW_SILENT = 1e-7f; // the low reverb's level: 140 dB down
    LowShape low = LOW;
    Tank low_tank;
    BiquadChain<2> low_band{};
    BiquadChainState<2> low_state{};
    bool low_on = false, low_idle = true;
    float low_level = 0.0f, low_target = 0.0f, low_time_gain = 1.0f;
    array<EarlyResponse, MODE_COUNT> early;
    bool early_enabled = true;
    array<DecayShape, MODE_COUNT> decays = DECAYS;
    SpringShape spring_shape = SPRING;
    array<Tank, MODE_COUNT> slow_tanks;
    float slow_level = 0.0f; // the slower tank's level at this Time
    array<BiquadChain<4>, MODE_COUNT> eq{};
    array<BiquadChainState<4>, 2> eq_state{};
    BiquadChain<3> tone{};
    array<BiquadChainState<3>, 2> tone_state{};
    float last_time = -1.0f, last_tone = -1.0f, time_gain = 1.0f;
    float level = 0.0f, target_level = 0.0f, smooth_k = 1.0f;
};

} // namespace rv6
} // namespace namx
