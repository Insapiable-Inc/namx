#include "PhaserProcessor.h"

#include <algorithm>
#include <cmath>

#include "Common.h"
#include "Constants.h"

namespace namx
{

void PhaserProcessor::reset() {
    lfo_phase = 0.0f;
    circuit_l.reset();
    circuit_r.reset();
}

float PhaserProcessor::gate_vgs(float phase, float depth) {
    static constexpr float RISE = 0.65f;
    const float p = phase - std::floor(phase);
    const float tri = p < RISE ? p / RISE : 1.0f - (p - RISE) / (1.0f - RISE);
    return Phase90Circuit::GATE_BOTTOM_V + 2.0f * depth * Phase90Circuit::GATE_SWING_V * tri;
}

void PhaserProcessor::render(float* l, float* r, size_t nframes, bool stereo_lfo) {
    const float depth = depth_pct.load() / 100.0f;
    // The circuit runs away from ~98 % feedback; the knob stops at 90.
    const float fb_amt =
        std::min(feedback_pct.load(), static_cast<float>(NAMConstants::PHASER_FEEDBACK_MAX)) /
        100.0f;
    const float mix = mix_pct.load() / 100.0f;
    const float ph_inc = rate_hz.load() / static_cast<float>(sample_rate);
    for (size_t i = 0; i < nframes; ++i) {
        const float vgs_l = gate_vgs(lfo_phase, depth);
        l[i] = circuit_l.process(l[i] * VOLTS_PER_UNIT, vgs_l, fb_amt, mix) / VOLTS_PER_UNIT;
        if (r != nullptr) {
            const float vgs_r = stereo_lfo ? gate_vgs(lfo_phase + 0.5f, depth) : vgs_l;
            r[i] = circuit_r.process(r[i] * VOLTS_PER_UNIT, vgs_r, fb_amt, mix) / VOLTS_PER_UNIT;
        }
        lfo_phase += ph_inc;
        if (lfo_phase >= 1.0f) {
            lfo_phase -= 1.0f;
        }
    }
}

} // namespace namx
