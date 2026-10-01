#include "NoiseGateProcessor.h"

#include <algorithm>
#include <cmath>

#include "Common.h"

namespace npl
{

void NoiseGateProcessor::reset() {
    envelope[0] = envelope[1] = 0.0f;
    gain_per_channel[0] = gain_per_channel[1] = 0.0f;
}

void NoiseGateProcessor::process_channel(float* buf, size_t nframes, int ch) {
    const float thresh_lin = std::pow(10.0f, threshold_db.load() / 20.0f);
    const float fs = static_cast<float>(sample_rate);
    const float at_ms = attack_ms.load();
    const float rel_ms = release_ms.load();

    const float at_coeff = std::exp(-1.0f / (fs * at_ms * 0.001f));
    const float rel_coeff = std::exp(-1.0f / (fs * rel_ms * 0.001f));

    float& env = envelope[ch];
    float& gain = gain_per_channel[ch];
    float min_gain = 1.0f;

    for (size_t i = 0; i < nframes; ++i) {
        float level = std::abs(buf[i]);
        if (level > env) {
            env = at_coeff * env + (1.0f - at_coeff) * level;
        } else {
            env = rel_coeff * env + (1.0f - rel_coeff) * level;
        }

        float target = (env >= thresh_lin) ? 1.0f : 0.0f;

        if (target > gain) {
            gain = at_coeff * gain + (1.0f - at_coeff) * target;
        } else {
            gain = rel_coeff * gain + (1.0f - rel_coeff) * target;
        }

        min_gain = std::min(min_gain, gain);
        buf[i] *= gain;
    }
    // One log per block, not per sample.
    const float gr_db = min_gain > 1e-3f ? -20.0f * std::log10(min_gain) : MAX_GR_DB;
    if (gr_db > gr_peak_db.load(std::memory_order_relaxed)) {
        gr_peak_db.store(std::min(gr_db, MAX_GR_DB), std::memory_order_relaxed);
    }
}

void NoiseGateProcessor::process_block_mono(float* buf, size_t nframes) {
    process_channel(buf, nframes, 0);
}

void NoiseGateProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    process_channel(l, nframes, 0);
    process_channel(r, nframes, 1);
}

} // namespace npl
