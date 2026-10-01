#include "PhaserProcessor.h"

#include <algorithm>
#include <cmath>

static constexpr float K_PI = 3.14159265358979323846f;

void PhaserProcessor::reset() {
    m_x1_l.fill(0.f);
    m_y1_l.fill(0.f);
    m_x1_r.fill(0.f);
    m_y1_r.fill(0.f);
    m_fb_state_l = m_fb_state_r = 0.0f;
    m_phase = 0.0f;
}

float PhaserProcessor::coeff_at(float phase, float depth) const {
    float lfo = 0.5f * (1.0f + std::sin(2.0f * K_PI * phase));
    float fc = K_MIN_FREQ * std::pow(K_MAX_FREQ / K_MIN_FREQ, depth * lfo);
    float w = std::tan(K_PI * fc / static_cast<float>(m_sample_rate));
    return (w - 1.0f) / (w + 1.0f);
}

void PhaserProcessor::vibe_coeffs(float phase, float depth, std::array<float, K_STAGES>& d) const {
    // Lamp + LDR: the light lingers bright and drops quickly — a skewed LFO.
    const float s = 0.5f * (1.0f + std::sin(2.0f * K_PI * phase));
    const float lfo = std::pow(s, 1.8f);
    static constexpr float K_RATIO[K_STAGES] = {1.0f, 1.65f, 2.7f, 4.4f};
    const float base = 90.0f * std::pow(1500.0f / 90.0f, depth * lfo);
    for (size_t k = 0; k < K_STAGES; ++k) {
        const float fc = std::min(base * K_RATIO[k], 0.45f * static_cast<float>(m_sample_rate));
        const float w = std::tan(K_PI * fc / static_cast<float>(m_sample_rate));
        d[k] = (w - 1.0f) / (w + 1.0f);
    }
}

float PhaserProcessor::process_vibe(float in, const std::array<float, K_STAGES>& d,
                                    std::array<float, K_STAGES>& x1,
                                    std::array<float, K_STAGES>& y1, float mix) const {
    float sig = in;
    for (size_t s = 0; s < K_STAGES; ++s) {
        float y = d[s] * (sig - y1[s]) + x1[s];
        x1[s] = sig;
        y1[s] = y;
        sig = y;
    }
    return in * (1.0f - mix) + sig * mix;
}

float PhaserProcessor::process_channel(float in, float d, std::array<float, K_STAGES>& x1,
                                       std::array<float, K_STAGES>& y1, float& fb_state,
                                       float fb_amt, float mix) const {
    float sig = in + fb_amt * fb_state;

    for (size_t s = 0; s < K_STAGES; ++s) {
        float y = d * (sig - y1[s]) + x1[s];
        x1[s] = sig;
        y1[s] = y;
        sig = y;
    }

    fb_state = sig;
    return in * (1.0f - mix) + sig * mix;
}

void PhaserProcessor::process_block_mono(float* buf, size_t nframes) {
    const float depth = m_depth.load() / 100.0f;
    const float fb_amt = m_feedback.load() / 100.0f;
    const float mix = m_mix.load() / 100.0f;
    const float ph_inc = m_rate.load() / static_cast<float>(m_sample_rate);

    const bool vibe = m_vibe.load(std::memory_order_relaxed);
    float d = 0.0f;
    std::array<float, K_STAGES> dv{};
    for (size_t i = 0; i < nframes; ++i) {
        if (i % K_CTRL_INTERVAL == 0) {
            if (vibe) {
                vibe_coeffs(m_phase, depth, dv);
            } else {
                d = coeff_at(m_phase, depth);
            }
        }
        buf[i] = vibe ? process_vibe(buf[i], dv, m_x1_l, m_y1_l, mix)
                      : process_channel(buf[i], d, m_x1_l, m_y1_l, m_fb_state_l, fb_amt, mix);
        m_phase += ph_inc;
        if (m_phase >= 1.0f) {
            m_phase -= 1.0f;
        }
    }
}

void PhaserProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    const float depth = m_depth.load() / 100.0f;
    const float fb_amt = m_feedback.load() / 100.0f;
    const float mix = m_mix.load() / 100.0f;
    const float ph_inc = m_rate.load() / static_cast<float>(m_sample_rate);

    const bool vibe = m_vibe.load(std::memory_order_relaxed);
    float d = 0.0f;
    std::array<float, K_STAGES> dv{};
    for (size_t i = 0; i < nframes; ++i) {
        // Same LFO phase for both channels: identical mono effect per channel.
        if (i % K_CTRL_INTERVAL == 0) {
            if (vibe) {
                vibe_coeffs(m_phase, depth, dv);
            } else {
                d = coeff_at(m_phase, depth);
            }
        }
        l[i] = vibe ? process_vibe(l[i], dv, m_x1_l, m_y1_l, mix)
                    : process_channel(l[i], d, m_x1_l, m_y1_l, m_fb_state_l, fb_amt, mix);
        r[i] = vibe ? process_vibe(r[i], dv, m_x1_r, m_y1_r, mix)
                    : process_channel(r[i], d, m_x1_r, m_y1_r, m_fb_state_r, fb_amt, mix);
        m_phase += ph_inc;
        if (m_phase >= 1.0f) {
            m_phase -= 1.0f;
        }
    }
}

void PhaserProcessor::process_block_stereo_out(float* l, float* r, size_t nframes) {
    const float depth = m_depth.load() / 100.0f;
    const float fb_amt = m_feedback.load() / 100.0f;
    const float mix = m_mix.load() / 100.0f;
    const float ph_inc = m_rate.load() / static_cast<float>(m_sample_rate);

    const bool vibe = m_vibe.load(std::memory_order_relaxed);
    float d_l = 0.0f, d_r = 0.0f;
    std::array<float, K_STAGES> dv_l{}, dv_r{};
    for (size_t i = 0; i < nframes; ++i) {
        if (i % K_CTRL_INTERVAL == 0) {
            if (vibe) {
                vibe_coeffs(m_phase, depth, dv_l);
                vibe_coeffs(m_phase + 0.5f, depth, dv_r);
            } else {
                d_l = coeff_at(m_phase, depth);
                d_r = coeff_at(m_phase + 0.5f, depth); // 180° apart
            }
        }
        l[i] = vibe ? process_vibe(l[i], dv_l, m_x1_l, m_y1_l, mix)
                    : process_channel(l[i], d_l, m_x1_l, m_y1_l, m_fb_state_l, fb_amt, mix);
        r[i] = vibe ? process_vibe(r[i], dv_r, m_x1_r, m_y1_r, mix)
                    : process_channel(r[i], d_r, m_x1_r, m_y1_r, m_fb_state_r, fb_amt, mix);
        m_phase += ph_inc;
        if (m_phase >= 1.0f) {
            m_phase -= 1.0f;
        }
    }
}
