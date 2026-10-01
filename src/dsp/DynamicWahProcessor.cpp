#include "DynamicWahProcessor.h"

#include <algorithm>
#include <cmath>

static constexpr float K_PI = 3.14159265358979323846f;

void DynamicWahProcessor::on_sample_rate_changed(double sr) {
    m_env_attack = static_cast<float>(std::exp(-1.0 / (0.002 * sr)));
    m_env_release = static_cast<float>(std::exp(-1.0 / (0.080 * sr)));
    reset();
}

void DynamicWahProcessor::reset() {
    m_env[0] = m_env[1] = 0.0f;
    m_svf_lp[0] = m_svf_lp[1] = 0.0f;
    m_svf_bp[0] = m_svf_bp[1] = 0.0f;
}

void DynamicWahProcessor::process_channel(float* buf, size_t nframes, int ch) {
    const float sens = m_sensitivity.load() / 100.0f;
    const float res = m_resonance.load() / 100.0f;
    const float mix = m_mix.load() / 100.0f;
    const float q_inv = 1.0f / (0.5f + res * 7.5f);
    const float sr = static_cast<float>(m_sample_rate);

    const bool pedal = m_pedal.load(std::memory_order_relaxed);
    const float pos_tgt =
        std::clamp(m_position.load(std::memory_order_relaxed), 0.0f, 100.0f) / 100.0f;
    const float pos_k = 1.0f - std::exp(-1.0f / (0.015f * sr)); // ~15 ms pedal glide
    float& pos = m_pos_smooth[ch];

    float& env = m_env[ch];
    float& lp = m_svf_lp[ch];
    float& bp = m_svf_bp[ch];

    for (size_t i = 0; i < nframes; ++i) {
        const float dry = buf[i];
        float level = std::abs(dry);
        float coeff = (level > env) ? m_env_attack : m_env_release;
        env += (level - env) * (1.0f - coeff);

        float cutoff;
        if (pedal) {
            // Rocker travel maps exponentially, like a real wah's pot taper.
            pos += pos_k * (pos_tgt - pos);
            cutoff = K_BASE_FREQ * std::pow(K_PEAK_FREQ / K_BASE_FREQ, pos);
        } else {
            cutoff = std::max(
                K_BASE_FREQ,
                std::min(K_BASE_FREQ + sens * env * (K_PEAK_FREQ - K_BASE_FREQ), K_PEAK_FREQ));
        }
        float f = std::min(2.0f * K_PI * cutoff / sr, 0.5f);

        float hp = dry - q_inv * bp - lp;
        bp += f * hp;
        lp += f * bp;

        buf[i] = bp * mix + dry * (1.0f - mix);
    }
}

void DynamicWahProcessor::process_block_mono(float* buf, size_t nframes) {
    process_channel(buf, nframes, 0);
}

void DynamicWahProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    process_channel(l, nframes, 0);
    process_channel(r, nframes, 1);
}
