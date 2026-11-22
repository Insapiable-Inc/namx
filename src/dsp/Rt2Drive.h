#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "Oversampler4x.h"

namespace namx::rt2
{

// The RT-2's Drive, fitted to the parked-rotor sessions of 2026-11-21
// (captures/boss/rt-2/README.md → Mode III's Drive, Modes I and II's Drive). With the rotors
// stopped each rotor is a fixed filter, so its response can be divided out and the Drive's
// own output read directly. Every mode has the same stage:
//
//   gain in (the knob) → y = F(x − c) → gain out (the knob) → a DC blocker
//
// F is an odd curve of two parabolas with a continuous slope: A·v² up to V1 (a square-law
// dead zone), then the slope falling linearly to zero at V3, flat after (the ceiling, TOP).
// c is a bias. A leak pulls it away from the curve's centre, towards C_FAR; the signal's
// excursions more than THETA from c pull it back at PULL per second per unit of excess
// (the excess capped at CAP):
//
//   dc/dt = (C_FAR − c) / LEAK_S + PULL · G(x − c),   G odd: sign · clamp(|u| − THETA, 0, CAP)
//
// At rest c sits just outside THETA, so a quiet signal rides the curve's side, nearly clean;
// a louder one pulls the dead zone under itself within 5–40 ms (crossover distortion, then
// the ceiling), and it drifts back out over a few hundred ms once the playing stops. The
// knob only moves the gains: every position, in every mode, is the same curve.
//
// The modes differ in the gains and in the filters round the curve:
//
// - Modes I and II (one law: their gains agree within 0.4 dB): about 19 dB less gain into the
//   curve than Mode III at the same position, and at 7:00 the Drive is out of the path
//   altogether (clean to the guitar's peak). With it on, a one-pole low-pass at 5.5 kHz
//   follows the curve. Between 7:00 and 9:00 nothing was measured: the gain in comes up from
//   −60 dB and the low-pass fades in.
// - Mode III: never out of the path, so its rotors' filters, fitted through it at 7:00, hold
//   whatever follows the curve. Ahead of the curve it has a one-pole low-pass at 5 kHz, which
//   those filters hold too: its inverse follows the curve here.
//
// Levels are re the guitar's peak (1.0). The curve runs at 4× the rate (the pedal's own shows
// little aliasing). Mono; nothing here allocates.
class Drive
{
public:
    static constexpr int MODE_III = 2; // modes 0–2: I, II, III

    // The oversampling filters' delay, in base-rate samples: the same in every mode and at
    // every position.
    static constexpr double latency_samples() {
        return Oversampler4x::latency_samples();
    }

    void prepare(double rate) {
        const double os_rate = 4.0 * rate;
        low_alpha = 1.0 - std::exp(-2.0 * std::numbers::pi * LOW_HZ / os_rate);
        leak = 1.0 - std::exp(-1.0 / (LEAK_S * os_rate));
        pull = PULL / os_rate;
        dc_pole = static_cast<float>(std::exp(-2.0 * std::numbers::pi * DC_HZ / rate));
        // The low-pass after the curve: a bilinear one-pole, the pedal's at 48 kHz.
        const double k = std::tan(std::numbers::pi * std::min(AFTER_HZ, 0.45 * rate) / rate);
        after_b = static_cast<float>(k / (1.0 + k));
        after_a = static_cast<float>((1.0 - k) / (1.0 + k));
        reset();
    }

    void reset() {
        oversampler.reset();
        low = 0.0;
        bias = REST;
        shaped_before = 0.0;
        after_in = after_out = 0.0f;
        dc_in = dc_out = 0.0f;
    }

    // The mode (0–2) and the Drive knob, 0–1 (7:00–5:00): the gains are linear in dB between
    // the five measured positions.
    void set(int mode, float pos) {
        third = mode == MODE_III;
        const Law& law = third ? LAW_III : LAW;
        gain_in = std::pow(10.0f, at(law.into_db, pos) / 20.0f);
        gain_out = std::pow(10.0f, (at(law.after_db, pos) + (third ? ROTORS_DB : 0.0f)) / 20.0f);
        clean = !third && pos <= 0.0f;
        after_mix = third ? 0.0f : std::clamp(pos / POSITION[1], 0.0f, 1.0f);
    }

    float process(float x) {
        // audio thread
        std::array<float, 4> s{};
        if (clean) {
            // Out of the path: only the oversampler's delay, so the latency holds.
            oversampler.up(x, s);
            const float y = oversampler.down(s);
            after_in = after_out = y;
            dc_in = dc_out = 0.0f;
            return y;
        }
        oversampler.up(x * gain_in, s);
        if (third) {
            for (float& v : s) {
                // The low-pass, the curve, the low-pass's inverse.
                low += low_alpha * (v - low);
                const double f = shape(low);
                v = static_cast<float>((f - (1.0 - low_alpha) * shaped_before) / low_alpha);
                shaped_before = f;
            }
        } else {
            for (float& v : s) {
                v = static_cast<float>(shape(v));
            }
        }
        float y = oversampler.down(s) * gain_out;
        const float lowered = after_b * (y + after_in) + after_a * after_out;
        after_in = y;
        after_out = lowered;
        y += after_mix * (lowered - y);
        const float out = y - dc_in + dc_pole * dc_out;
        dc_in = y;
        dc_out = out;
        return out;
    }

private:
    // The curve: 8.93·v² to −23.3 dB, the ceiling (−12.2 dB) reached at −7.9 dB.
    static constexpr double A = 8.93024;
    static constexpr double V1 = 0.068514;
    static constexpr double V3 = 0.401017;
    static constexpr double SLOPE = 2.0 * A * V1;
    static constexpr double B = SLOPE / (2.0 * (V3 - V1));
    static constexpr double TOP = A * V1 * V1 + SLOPE * (V3 - V1) / 2.0;
    // The bias: where the leak pulls it (−16.6 dB) and how slowly, how far from it the
    // signal starts to pull (−24.1 dB) and how hard.
    static constexpr double C_FAR = 0.148188;
    static constexpr double LEAK_S = 0.4307;
    static constexpr double THETA = 0.062497;
    static constexpr double CAP = 1.05944;
    static constexpr double PULL = 63.08;
    // Where the two balance with no signal (−23.7 dB), and the curve's output there: taken
    // off so silence comes out as silence. A quiet signal's gain through the curve is its
    // slope there, 2·A·REST (+1.37 dB).
    static constexpr double REST = (C_FAR + PULL * LEAK_S * THETA) / (1.0 + PULL * LEAK_S);
    static constexpr double IDLE = -A * REST * REST;
    // Mode III's low-pass ahead of the curve, Modes I and II's after it, the DC blocker.
    static constexpr double LOW_HZ = 5000.0;
    static constexpr double AFTER_HZ = 5522.0;
    static constexpr double DC_HZ = 10.0;

    // The knob: gain into the curve and after it, dB, at 7:00, 9:00, 12:00, 3:00, 5:00.
    struct Law
    {
        std::array<float, 5> into_db;
        std::array<float, 5> after_db;
    };
    static constexpr std::array<float, 5> POSITION{0.0f, 0.2f, 0.5f, 0.8f, 1.0f};
    // Mode III (after the curve: the fit's values less 0.13 dB, the guitar's level at the
    // pedal's outputs).
    static constexpr Law LAW_III{{0.0f, 18.63f, 27.74f, 32.02f, 33.33f},
                                 {0.07f, -11.47f, -14.85f, -15.44f, -15.49f}};
    // Modes I and II (the mean of the two fits). 7:00 is not a measured point of the curve
    // (the pedal takes the Drive out there): −60 dB in, and after it what brings a quiet
    // signal's gain to 0 dB, so the Drive comes in from clean.
    static constexpr Law LAW{{-60.0f, -1.67f, 8.38f, 13.03f, 14.52f},
                             {58.63f, 0.41f, -7.58f, -10.13f, -10.70f}};
    // Mode III's rotors' filters were fitted on −12 dB white noise through the pedal at Drive
    // 7:00, which the pedal's Drive turned down by 1.22 dB: they hold that gain already.
    static constexpr float ROTORS_DB = 1.22f;

    static float at(const std::array<float, 5>& db, float pos) {
        pos = std::clamp(pos, 0.0f, 1.0f);
        size_t i = 0;
        while (i + 2 < POSITION.size() && pos > POSITION[i + 1]) {
            ++i;
        }
        const float t = (pos - POSITION[i]) / (POSITION[i + 1] - POSITION[i]);
        return db[i] + t * (db[i + 1] - db[i]);
    }

    // The curve read at the bias, one sample at the oversampled rate; the bias moves. In
    // doubles: with little gain in, the output is a small difference of the curve's values.
    double shape(double x) {
        const double u = x - bias;
        const double size = std::abs(u);
        const double excess = std::clamp(size - THETA, 0.0, CAP);
        bias += (C_FAR - bias) * leak + std::copysign(pull * excess, u);
        double f = TOP;
        if (size < V1) {
            f = A * size * size;
        } else if (size < V3) {
            f = TOP - B * (V3 - size) * (V3 - size);
        }
        return std::copysign(f, u) - IDLE;
    }

    Oversampler4x oversampler;
    double low_alpha = 1.0, leak = 0.0, pull = 0.0;
    float after_b = 0.5f, after_a = 0.0f, dc_pole = 0.0f;
    bool third = true, clean = false;
    float gain_in = 1.0f, gain_out = 1.0f, after_mix = 0.0f;
    double low = 0.0, bias = REST, shaped_before = 0.0;
    float after_in = 0.0f, after_out = 0.0f;
    float dc_in = 0.0f, dc_out = 0.0f;
};

} // namespace namx::rt2
