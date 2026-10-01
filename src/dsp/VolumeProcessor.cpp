#include "VolumeProcessor.h"

#include <algorithm>
#include <cmath>

float VolumeProcessor::target_gain() const {
    const float p = std::clamp(m_position_pct.load(), 0.0f, 100.0f) / 100.0f;
    if (p <= 0.0f) {
        return 0.0f;
    }
    const float mid = std::clamp(m_mid_db.load(), -40.0f, 0.0f);
    // dB linear in travel on each half: −60 dB → Mid → 0 dB.
    const float db =
        p < 0.5f ? -60.0f + (mid + 60.0f) * (p / 0.5f) : mid + (0.0f - mid) * ((p - 0.5f) / 0.5f);
    return p < 0.02f
               ? std::pow(10.0f, db / 20.0f) * (p / 0.02f) // fade the last bit to true silence
               : std::pow(10.0f, db / 20.0f);
}

bool VolumeProcessor::process(float* l, float* r, size_t n, bool input_is_stereo) {
    if (!m_enabled.load()) {
        return input_is_stereo;
    }
    const float target = target_gain();
    const float k = 1.0f - std::exp(-1.0f / (0.010f * static_cast<float>(m_sample_rate)));
    for (size_t i = 0; i < n; ++i) {
        m_gain += k * (target - m_gain);
        l[i] *= m_gain;
        if (input_is_stereo) {
            r[i] *= m_gain;
        }
    }
    if (!input_is_stereo) {
        std::copy(l, l + n, r);
    }
    return input_is_stereo;
}
