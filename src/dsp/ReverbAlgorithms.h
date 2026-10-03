#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "Common.h"
#include "DelayLine.h"

namespace namx
{

// The reverb's tanks. Each takes a stereo input and gives a stereo wet output, one
// sample at a time; the decay is set as an RT60 in seconds (the time the tail takes to
// fall 60 dB) and Damping as how much faster the highs die (0 = as fast as the lows,
// 1 = much faster). prepare() allocates (GUI thread / audio stopped); the rest doesn't.

namespace reverb_tanks
{

constexpr float TWO_PI = 6.28318530717958647692f;

// Lattice all-pass: w = x + g·d, y = d − g·w, d ← w. Delay of `length` samples.
struct AllPass
{
    DelayLine line;
    size_t length = 1;
    void prepare(size_t new_length) {
        length = std::max<size_t>(1, new_length);
        line.resize(length + 1);
    }
    void clear() {
        line.clear();
    }
    float process(float x, float g) {
        const float d = line.at(length - 1);
        const float w = x + g * d;
        line.push(w);
        return d - g * w;
    }
    // A tap inside the all-pass's own delay (`delay` samples behind its newest write).
    float tap(size_t delay) const {
        return line.at(delay);
    }
};

// The same with a modulated (fractional) length.
struct ModulatedAllPass
{
    DelayLine line;
    void prepare(size_t max_length) {
        line.resize(max_length + 4);
    }
    void clear() {
        line.clear();
    }
    float process(float x, float g, float length) {
        const float d = line.interpolate(length - 1.0f);
        const float w = x + g * d;
        line.push(w);
        return d - g * w;
    }
};

// A plain delay with taps.
struct Delay
{
    DelayLine line;
    size_t length = 1;
    void prepare(size_t new_length) {
        length = std::max<size_t>(1, new_length);
        line.resize(length + 1);
    }
    void clear() {
        line.clear();
    }
    float process(float x) {
        const float out = line.at(length - 1);
        line.push(x);
        return out;
    }
    float tap(size_t delay) const {
        return line.at(std::min(delay, length - 1));
    }
};

// Jon Dattorro's plate ("Effect Design, Part 1", JAES 1997, after Griesinger): a
// band-limited input through four diffusers into a "figure-8" tank of two halves, each
// a modulated all-pass, a delay, damping, decay, an all-pass and a delay, feeding the
// other half. The wet output is the paper's seven taps per side. Every length is the
// paper's, given at 29 761 Hz and scaled to the engine rate.
class Plate
{
public:
    void prepare(double sr) {
        rate = static_cast<float>(sr);
        scale = rate / 29761.0f;
        static constexpr size_t DIFFUSERS[4] = {142, 107, 379, 277};
        for (size_t i = 0; i < 4; ++i) {
            input_diffusers[i].prepare(len(DIFFUSERS[i]));
        }
        excursion = 16.0f * scale;
        mod[0].prepare(len(672) + static_cast<size_t>(excursion) + 2);
        mod[1].prepare(len(908) + static_cast<size_t>(excursion) + 2);
        long_delay[0].prepare(len(4453));
        long_delay[1].prepare(len(4217));
        decay_ap[0].prepare(len(1800));
        decay_ap[1].prepare(len(2656));
        end_delay[0].prepare(len(3720));
        end_delay[1].prepare(len(3163));
        // One pass through a half: the gain `decay` is applied twice per half.
        half_loop_seconds = static_cast<float>(len(672) + len(4453) + len(1800) + len(3720) +
                                               len(908) + len(4217) + len(2656) + len(3163)) /
                            (2.0f * rate);
        lfo_step = TWO_PI * 1.0f / rate;
        reset();
    }

    void reset() {
        for (auto& d : input_diffusers) {
            d.clear();
        }
        for (int h = 0; h < 2; ++h) {
            mod[h].clear();
            long_delay[h].clear();
            decay_ap[h].clear();
            end_delay[h].clear();
            damp_state[h] = 0.0f;
            half_out[h] = 0.0f;
        }
        bandwidth_state = 0.0f;
        lfo_phase = 0.0f;
    }

    void set_decay(float rt60_seconds, float damping) {
        // gain per half pass, applied twice: decay² per half_loop_seconds.
        decay = std::pow(10.0f, -3.0f * half_loop_seconds / (2.0f * rt60_seconds));
        decay = std::min(decay, 0.9999f);
        decay_diffusion_2 = std::clamp(decay + 0.15f, 0.25f, 0.5f);
        damping_coefficient = std::clamp(damping, 0.0f, 1.0f) * 0.7f + 0.0005f;
    }

    void process(float in_l, float in_r, float& out_l, float& out_r) {
        // Bandwidth (one-pole low-pass) and input diffusion.
        const float x = 0.5f * (in_l + in_r);
        bandwidth_state += BANDWIDTH * (x - bandwidth_state);
        float d = input_diffusers[0].process(bandwidth_state, 0.75f);
        d = input_diffusers[1].process(d, 0.75f);
        d = input_diffusers[2].process(d, 0.625f);
        d = input_diffusers[3].process(d, 0.625f);

        // Tank: two halves, each fed by the input and the other half's output.
        lfo_phase += lfo_step;
        if (lfo_phase > TWO_PI) {
            lfo_phase -= TWO_PI;
        }
        const float lfo[2] = {std::sin(lfo_phase), std::cos(lfo_phase)};
        const size_t base[2] = {len(672), len(908)};
        float new_out[2];
        for (int h = 0; h < 2; ++h) {
            float t = d + decay * half_out[1 - h];
            t = mod[h].process(t, -DECAY_DIFFUSION_1,
                               static_cast<float>(base[h]) + excursion * (0.5f + 0.5f * lfo[h]));
            t = long_delay[h].process(t);
            damp_state[h] += (1.0f - damping_coefficient) * (t - damp_state[h]);
            t = damp_state[h] * decay;
            t = decay_ap[h].process(t, decay_diffusion_2);
            new_out[h] = end_delay[h].process(t);
        }
        half_out[0] = new_out[0];
        half_out[1] = new_out[1];

        // The paper's output taps (left takes mostly from the right half, and vice versa).
        out_l = OUTPUT_GAIN * (long_delay[1].tap(len(266)) + long_delay[1].tap(len(2974)) -
                               decay_ap[1].tap(len(1913)) + end_delay[1].tap(len(1996)) -
                               long_delay[0].tap(len(1990)) - decay_ap[0].tap(len(187)) -
                               end_delay[0].tap(len(1066)));
        out_r = OUTPUT_GAIN * (long_delay[0].tap(len(353)) + long_delay[0].tap(len(3627)) -
                               decay_ap[0].tap(len(1228)) + end_delay[0].tap(len(2673)) -
                               long_delay[1].tap(len(2111)) - decay_ap[1].tap(len(335)) -
                               end_delay[1].tap(len(121)));
    }

private:
    static constexpr float BANDWIDTH = 0.9995f;
    static constexpr float DECAY_DIFFUSION_1 = 0.70f;
    static constexpr float OUTPUT_GAIN = 0.6f;

    size_t len(size_t at_29761) const {
        return std::max<size_t>(
            1, static_cast<size_t>(std::lround(static_cast<float>(at_29761) * scale)));
    }

    float rate = 48000.0f, scale = 1.0f;
    float excursion = 16.0f;
    float half_loop_seconds = 0.36f;
    float decay = 0.5f, decay_diffusion_2 = 0.5f, damping_coefficient = 0.0005f;
    float bandwidth_state = 0.0f;
    float lfo_phase = 0.0f, lfo_step = 0.0f;
    std::array<AllPass, 4> input_diffusers;
    ModulatedAllPass mod[2];
    Delay long_delay[2];
    AllPass decay_ap[2];
    Delay end_delay[2];
    float damp_state[2]{};
    float half_out[2]{};
};

// Hall and room: a feedback delay network of 8 or 16 lines. The lines are mixed through a
// normalised Hadamard matrix (lossless), each line ends in a first-order shelf that sets
// its loss per pass from the RT60 at DC and at the top (Jot), the line lengths are
// slowly modulated against metallic ringing, the input is smeared by four all-passes,
// and an early-reflection tap pattern gives the room its first echoes.
class Fdn
{
public:
    struct Shape
    {
        size_t count; // delay lines in use: 8 or 16
        std::array<float, 16> line_ms; // their lengths
        std::array<float, 4> diffuser_ms; // input diffusion
        float diffusion; // all-pass gain
        float modulation_ms; // peak line-length modulation
        std::array<float, 12> early_ms; // early reflection taps
        float early_level;
    };

    static constexpr Shape HALL = {
        8,
        {43.7f, 51.3f, 59.9f, 67.1f, 77.3f, 87.1f, 97.9f, 109.3f},
        {4.7f, 3.6f, 12.7f, 9.3f},
        0.70f,
        0.60f,
        {11.0f, 17.0f, 23.0f, 29.0f, 37.0f, 43.0f, 51.0f, 59.0f, 67.0f, 73.0f, 83.0f, 97.0f},
        0.35f};
    // 16 lines: with 8 short ones the tail rang (metallic, heard past a long pre-delay).
    static constexpr Shape ROOM = {
        16,
        {11.3f, 13.1f, 14.9f, 16.7f, 18.9f, 21.1f, 23.3f, 25.7f, 27.9f, 30.7f, 33.1f, 35.9f, 38.9f,
         41.9f, 44.9f, 47.9f},
        {2.1f, 1.7f, 4.3f, 3.1f},
        0.60f,
        0.50f,
        {2.3f, 3.7f, 5.3f, 6.7f, 8.9f, 10.7f, 12.1f, 14.3f, 16.9f, 19.3f, 22.1f, 25.7f},
        0.55f};

    void prepare(double sr, const Shape& new_shape) {
        rate = static_cast<float>(sr);
        shape = new_shape;
        const float ms = rate / 1000.0f;
        count = shape.count;
        for (size_t i = 0; i < count; ++i) {
            base_length[i] = shape.line_ms[i] * ms;
            lines[i].resize(static_cast<size_t>(base_length[i] + shape.modulation_ms * ms) + 4);
            lfo_step[i] = TWO_PI * (0.31f + 0.13f * static_cast<float>(i)) / rate;
            lfo_phase[i] = static_cast<float>(i) * 0.785f;
        }
        depth = shape.modulation_ms * ms;
        for (size_t i = 0; i < 4; ++i) {
            diffusers[i].prepare(static_cast<size_t>(shape.diffuser_ms[i] * ms));
            // The right input's diffusers are a little longer: decorrelated sides.
            diffusers_right[i].prepare(static_cast<size_t>(shape.diffuser_ms[i] * 1.13f * ms));
        }
        early.resize(static_cast<size_t>(shape.early_ms.back() * ms) + 2);
        for (size_t i = 0; i < 12; ++i) {
            early_tap[i] = static_cast<size_t>(shape.early_ms[i] * ms);
        }
        reset();
    }

    void reset() {
        for (auto& l : lines) {
            if (!l.empty()) {
                l.clear();
            }
        }
        for (auto& d : diffusers) {
            d.clear();
        }
        for (auto& d : diffusers_right) {
            d.clear();
        }
        early.clear();
        shelf_state.fill(0.0f);
    }

    void set_decay(float rt60_seconds, float damping) {
        const float rt_high = rt60_seconds * (1.0f - 0.9f * std::clamp(damping, 0.0f, 1.0f));
        for (size_t i = 0; i < count; ++i) {
            const float g_dc = std::pow(10.0f, -3.0f * base_length[i] / (rate * rt60_seconds));
            const float g_hf = std::pow(10.0f, -3.0f * base_length[i] / (rate * rt_high));
            // H(z) = b0 / (1 − a1·z⁻¹) with H(1) = g_dc, H(−1) = g_hf.
            shelf_a1[i] = (g_dc - g_hf) / (g_dc + g_hf);
            shelf_b0[i] = g_dc * (1.0f - shelf_a1[i]);
        }
    }

    void process(float in_l, float in_r, float& out_l, float& out_r) {
        // Early reflections from the dry input, alternating sides.
        const float mono = 0.5f * (in_l + in_r);
        early.push(mono);
        float er_l = 0.0f, er_r = 0.0f;
        for (size_t i = 0; i < 12; ++i) {
            const float g = 1.0f - static_cast<float>(i) / 14.0f;
            (i % 2 == 0 ? er_l : er_r) += g * early.at(early_tap[i]);
        }

        // Diffused input, left into the even lines and right into the odd ones.
        float dl = in_l, dr = in_r;
        for (size_t i = 0; i < 4; ++i) {
            dl = diffusers[i].process(dl, shape.diffusion);
        }
        for (size_t i = 0; i < 4; ++i) {
            dr = diffusers_right[i].process(dr, shape.diffusion);
        }

        // Read the lines (modulated length), through their loss shelves.
        std::array<float, N> y{};
        for (size_t i = 0; i < count; ++i) {
            lfo_phase[i] += lfo_step[i];
            if (lfo_phase[i] > TWO_PI) {
                lfo_phase[i] -= TWO_PI;
            }
            const float length = base_length[i] + depth * (0.5f + 0.5f * std::sin(lfo_phase[i]));
            const float raw = lines[i].interpolate(length - 1.0f);
            shelf_state[i] = shelf_b0[i] * raw + shelf_a1[i] * shelf_state[i];
            y[i] = shelf_state[i];
        }

        // Outputs: two orthogonal sign patterns (Hadamard rows 1 and 2), scaled so 8 and 16
        // lines come out at the same level.
        float sum_l = 0.0f, sum_r = 0.0f;
        for (size_t i = 0; i < count; ++i) {
            sum_l += i % 2 == 0 ? y[i] : -y[i];
            sum_r += (i / 2) % 2 == 0 ? y[i] : -y[i];
        }
        const float level = OUTPUT_GAIN * std::sqrt(8.0f / static_cast<float>(count));
        out_l = level * sum_l + shape.early_level * er_l * EARLY_GAIN;
        out_r = level * sum_r + shape.early_level * er_r * EARLY_GAIN;

        // Feedback through the normalised Hadamard matrix (fast transform), plus input.
        hadamard(y, count);
        for (size_t i = 0; i < count; ++i) {
            lines[i].push(y[i] + (i % 2 == 0 ? dl : dr) * INPUT_GAIN);
        }
    }

private:
    static constexpr size_t N = 16; // the most lines a shape may use
    static constexpr float OUTPUT_GAIN = 0.35f;
    static constexpr float INPUT_GAIN = 0.5f;
    static constexpr float EARLY_GAIN = 0.25f;

    static void hadamard(std::array<float, N>& v, size_t n) {
        for (size_t h = 1; h < n; h *= 2) {
            for (size_t i = 0; i < n; i += 2 * h) {
                for (size_t j = i; j < i + h; ++j) {
                    const float a = v[j], b = v[j + h];
                    v[j] = a + b;
                    v[j + h] = a - b;
                }
            }
        }
        const float norm = 1.0f / std::sqrt(static_cast<float>(n));
        for (size_t i = 0; i < n; ++i) {
            v[i] *= norm;
        }
    }

    float rate = 48000.0f;
    Shape shape = HALL;
    size_t count = 8;
    std::array<DelayLine, N> lines;
    std::array<float, N> base_length{}, lfo_phase{}, lfo_step{};
    float depth = 0.0f;
    std::array<float, N> shelf_b0{}, shelf_a1{}, shelf_state{};
    std::array<AllPass, 4> diffusers, diffusers_right;
    DelayLine early;
    std::array<size_t, 12> early_tap{};
};

} // namespace reverb_tanks
} // namespace namx
