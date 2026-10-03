#include "DynamicWahProcessor.h"

#include <algorithm>
#include <cmath>

#include "Common.h"

namespace namx
{

static constexpr float PI = 3.14159265358979323846f;

void DynamicWahProcessor::on_sample_rate_changed(double sr) {
    env_attack = static_cast<float>(std::exp(-1.0 / (0.002 * sr)));
    env_release = static_cast<float>(std::exp(-1.0 / (0.080 * sr)));
    reset();
}

void DynamicWahProcessor::reset() {
    env_per_channel[0] = env_per_channel[1] = 0.0f;
    svf_lp[0] = svf_lp[1] = 0.0f;
    svf_bp[0] = svf_bp[1] = 0.0f;
}

void DynamicWahProcessor::process_channel(float* buf, size_t nframes, int ch) {
    const float sens = sensitivity_pct.load() / 100.0f;
    const float res = resonance_pct.load() / 100.0f;
    const float mix = mix_pct.load() / 100.0f;
    const float q_inv = 1.0f / (0.5f + res * 7.5f);
    const float sr = static_cast<float>(sample_rate);

    const bool current_pedal = pedal.load(std::memory_order_relaxed);
    const float pos_tgt =
        std::clamp(position_pct.load(std::memory_order_relaxed), 0.0f, 100.0f) / 100.0f;
    const float pos_k = 1.0f - std::exp(-1.0f / (0.015f * sr)); // ~15 ms pedal glide
    float& pos = pos_smooth[ch];

    float& env = env_per_channel[ch];
    float& lp = svf_lp[ch];
    float& bp = svf_bp[ch];

    for (size_t i = 0; i < nframes; ++i) {
        const float dry = buf[i];
        float level = std::abs(dry);
        float coeff = (level > env) ? env_attack : env_release;
        env += (level - env) * (1.0f - coeff);

        float cutoff;
        if (current_pedal) {
            // Rocker travel maps exponentially, like a real wah's pot taper.
            pos += pos_k * (pos_tgt - pos);
            cutoff = BASE_FREQ * std::pow(PEAK_FREQ / BASE_FREQ, pos);
        } else {
            cutoff = std::max(
                BASE_FREQ, std::min(BASE_FREQ + sens * env * (PEAK_FREQ - BASE_FREQ), PEAK_FREQ));
        }
        float f = std::min(2.0f * PI * cutoff / sr, 0.5f);

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

} // namespace namx
