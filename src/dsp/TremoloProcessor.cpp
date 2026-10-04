#include "TremoloProcessor.h"

#include <cmath>
#include <numbers>

#include "Common.h"

namespace namx
{

void TremoloProcessor::process_block_mono(float* buf, size_t nframes) {
    const float rate = rate_hz.load();
    const float depth = depth_pct.load() / 100.0f;
    const float ph_inc = rate / static_cast<float>(sample_rate);

    for (size_t i = 0; i < nframes; ++i) {
        float lfo = std::sin(2.0f * std::numbers::pi_v<float> * phase);
        float gain = 1.0f - depth * (1.0f - lfo) * 0.5f;
        buf[i] *= gain;

        phase += ph_inc;
        if (phase >= 1.0f) {
            phase -= 1.0f;
        }
    }
}

void TremoloProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    const float rate = rate_hz.load();
    const float depth = depth_pct.load() / 100.0f;
    const float ph_inc = rate / static_cast<float>(sample_rate);

    for (size_t i = 0; i < nframes; ++i) {
        // Same LFO gain on both channels: identical mono effect per channel.
        float lfo = std::sin(2.0f * std::numbers::pi_v<float> * phase);
        float gain = 1.0f - depth * (1.0f - lfo) * 0.5f;
        l[i] *= gain;
        r[i] *= gain;

        phase += ph_inc;
        if (phase >= 1.0f) {
            phase -= 1.0f;
        }
    }
}

void TremoloProcessor::process_block_stereo_out(float* l, float* r, size_t nframes) {
    const float rate = rate_hz.load();
    const float depth = depth_pct.load() / 100.0f;
    const float ph_inc = rate / static_cast<float>(sample_rate);

    for (size_t i = 0; i < nframes; ++i) {
        float lfo_l = std::sin(2.0f * std::numbers::pi_v<float> * phase);
        float lfo_r = std::sin(2.0f * std::numbers::pi_v<float> *
                               (phase + 0.5f)); // 180° apart: true auto-pan
        l[i] *= 1.0f - depth * (1.0f - lfo_l) * 0.5f;
        r[i] *= 1.0f - depth * (1.0f - lfo_r) * 0.5f;

        phase += ph_inc;
        if (phase >= 1.0f) {
            phase -= 1.0f;
        }
    }
}

} // namespace namx
