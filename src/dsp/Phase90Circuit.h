#pragma once

#include <numbers>

#include "Common.h"

namespace namx
{

// The MXR Phase 90's audio path ("script" circuit), mono, one per channel. After
// R. Giampiccolo, S. Del Moro, C. Eutizi, M. Massimi, O. Massi, A. Bernardini, "Wave
// Digital Model of the MXR Phase 90 based on a Time-Varying Resistor Approximation of
// JFET Elements" — its MATLAB reference (github.com/polimi-ispl/mxrphase90, MIT,
// © 2024 R. Giampiccolo, S. Del Moro, C. Eutizi, M. Massimi) reduced to closed form;
// matches it to ~1e-6 V.
//
// Input buffer: 10 nF into 10 k + 470 k to Vref (a 33 Hz high-pass, ×470/480). Then
// four identical stages: the stage input drives an op-amp's − input through 10 k (10 k
// feedback) and its + input through 47 nF, with 24 k in parallel with a 2N5952 JFET's
// channel from + to Vref — an all-pass whose corner follows the JFET. The LFO drives
// the four gates; the channel resistance also depends on the signal across it (Vds), so
// the sweep's low end, close to pinch-off, adds asymmetric distortion — the pedal's
// grit, not a bug. Dry + wet are summed at the output (here by Mix) through a coupling
// capacitor (≈ 17 Hz high-pass, which also removes the DC that rectification makes).
//
// Each capacitor is trapezoidal (as the WDF is); Vds is taken from the previous sample
// (as in the reference), which makes each sample linear, so Feedback — not in the
// script circuit; the "block" one feeds the last stage back — is solved without a
// one-sample delay: the four stages are an affine map y = A·u + B of their input. The
// feedback path is AC-coupled (20 Hz): the JFETs' rectified DC would otherwise build up
// by 1 / (1 − feedback). Keep feedback ≤ 0.9: from ~0.98 the loop rings on its own and
// runs away (there are no op-amp rails here; clamping to them sounded harsh).
// Signals are in volts (the caller scales); nothing allocates.
class Phase90Circuit
{
public:
    // The four gates sit this far below Vref at the bottom of the sweep. The
    // reference's LFO spans 3.1–3.4 V against Vref = 5.1 V.
    static constexpr float GATE_BOTTOM_V = 3.1f - 5.1f;
    static constexpr float GATE_SWING_V = 0.3f;

    void prepare(double sr) {
        const double t = 1.0 / sr;
        g_in = static_cast<float>(t / (2.0 * (IN_R_SERIES + IN_R_BIAS) * IN_C));
        g_stage_per_siemens = static_cast<float>(t / (2.0 * STAGE_C));
        g_fb = static_cast<float>(t * std::numbers::pi * FB_HP_HZ);
        g_out = static_cast<float>(t * std::numbers::pi * OUT_HP_HZ);
        reset();
    }

    void reset() {
        s_in = s_fb = s_out = 0.0f;
        s.fill(0.0f);
        vds.fill(0.0f);
    }

    // One sample. vgs: gate − source voltage (GATE_BOTTOM_V … + GATE_SWING_V for the
    // pedal's sweep); feedback 0…<1; mix 0…1 (the pedal is 0.5, at twice the level).
    float process(float v_in, float vgs, float feedback, float mix) {
        // Input buffer.
        const float vc_in = (s_in + g_in * v_in) / (1.0f + g_in);
        s_in = 2.0f * vc_in - s_in;
        const float dry = (v_in - vc_in) * IN_GAIN;

        // JFET saturation current at this Vgs, and the overdrive above pinch-off.
        const float overdrive = vgs - VP;
        const float pinch = 1.0f - vgs / VP;
        const float i_sat = IS0 * pinch * pinch;

        // Per stage: the corner's g, and the stage as out = a·in + b.
        array<float, STAGES> g{};
        array<float, STAGES> inv{};
        float a_total = 1.0f;
        float b_total = 0.0f;
        for (size_t k = 0; k < STAGES; ++k) {
            const float conductance = G_PARALLEL + jfet_conductance(overdrive, i_sat, vds[k]);
            g[k] = g_stage_per_siemens * conductance;
            inv[k] = 1.0f / (1.0f + g[k]);
            const float a = (1.0f - g[k]) * inv[k];
            const float b = -2.0f * s[k] * inv[k];
            a_total *= a;
            b_total = a * b_total + b;
        }
        // The feedback's DC blocker is affine too: fb_signal = fb_inv · wet + fb_offset.
        const float fb_inv = 1.0f / (1.0f + g_fb);
        const float fb_offset = -s_fb * fb_inv;
        const float wet = (a_total * (dry + feedback * fb_offset) + b_total) /
                          (1.0f - feedback * fb_inv * a_total);
        const float vc_fb = (s_fb + g_fb * wet) * fb_inv;
        s_fb = 2.0f * vc_fb - s_fb;

        // Run the stages on the solved input to advance their state.
        float x = dry + feedback * (wet - vc_fb);
        for (size_t k = 0; k < STAGES; ++k) {
            const float vc = (s[k] + g[k] * x) * inv[k];
            s[k] = 2.0f * vc - s[k];
            const float v_plus = x - vc;
            vds[k] = v_plus;
            x = 2.0f * v_plus - x;
        }

        // Output coupling.
        const float mixed = dry * (1.0f - mix) + x * mix;
        const float vc_out = (s_out + g_out * mixed) / (1.0f + g_out);
        s_out = 2.0f * vc_out - s_out;
        return mixed - vc_out;
    }

private:
    static constexpr size_t STAGES = 4;

    // 2N5952 (the reference's values).
    static constexpr float IS0 = 5.367e-3f; // saturation current at Vgs = 0
    static constexpr float VP = -2.021f; // pinch-off voltage
    static constexpr float K = 2.0f * IS0 / (VP * VP);
    static constexpr float LAMBDA = 4e-3f; // channel-length modulation

    // Channel conductance: triode below Vds = Vgs − Vp, saturation above, off when
    // pinched (the reference's 1 TΩ).
    static float jfet_conductance(float overdrive, float i_sat, float v_ds) {
        if (overdrive < 0.0f) {
            return 0.0f;
        }
        if (v_ds <= overdrive) {
            return K * (overdrive - 0.5f * v_ds);
        }
        return i_sat * (1.0f + LAMBDA * v_ds) / v_ds; // v_ds > overdrive ≥ 0
    }

    static constexpr double IN_C = 0.01e-6;
    static constexpr double IN_R_SERIES = 10e3;
    static constexpr double IN_R_BIAS = 470e3;
    static constexpr float IN_GAIN = static_cast<float>(IN_R_BIAS / (IN_R_SERIES + IN_R_BIAS));
    static constexpr double STAGE_C = 47e-9;
    static constexpr float G_PARALLEL = 1.0f / 24e3f;
    static constexpr double OUT_HP_HZ = 17.4;
    static constexpr double FB_HP_HZ = 20.0;

    float g_in{0.0f};
    float g_stage_per_siemens{0.0f};
    float g_fb{0.0f};
    float g_out{0.0f};
    float s_in{0.0f};
    float s_fb{0.0f};
    float s_out{0.0f};
    array<float, STAGES> s{};
    array<float, STAGES> vds{};
};

} // namespace namx
