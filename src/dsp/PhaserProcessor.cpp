#include "PhaserProcessor.h"

#include <algorithm>
#include <cmath>

static constexpr float PI = 3.14159265358979323846f;

void PhaserProcessor::reset() {
    x1_l.fill(0.f);
    y1_l.fill(0.f);
    x1_r.fill(0.f);
    y1_r.fill(0.f);
    fb_state_l = fb_state_r = 0.0f;
    lfo_phase = 0.0f;
}

float PhaserProcessor::coeff_at(float phase, float depth) const {
    float lfo = 0.5f * (1.0f + std::sin(2.0f * PI * phase));
    float fc = MIN_FREQ * std::pow(MAX_FREQ / MIN_FREQ, depth * lfo);
    float w = std::tan(PI * fc / static_cast<float>(sample_rate));
    return (w - 1.0f) / (w + 1.0f);
}

void PhaserProcessor::vibe_coeffs(float phase, float depth, std::array<float, STAGES>& d) const {
    // Lamp + LDR: the light lingers bright and drops quickly — a skewed LFO.
    const float s = 0.5f * (1.0f + std::sin(2.0f * PI * phase));
    const float lfo = std::pow(s, 1.8f);
    static constexpr float RATIO[STAGES] = {1.0f, 1.65f, 2.7f, 4.4f};
    const float base = 90.0f * std::pow(1500.0f / 90.0f, depth * lfo);
    for (size_t k = 0; k < STAGES; ++k) {
        const float fc = std::min(base * RATIO[k], 0.45f * static_cast<float>(sample_rate));
        const float w = std::tan(PI * fc / static_cast<float>(sample_rate));
        d[k] = (w - 1.0f) / (w + 1.0f);
    }
}

float PhaserProcessor::process_vibe(float in, const std::array<float, STAGES>& d,
                                    std::array<float, STAGES>& x1, std::array<float, STAGES>& y1,
                                    float mix) const {
    float sig = in;
    for (size_t s = 0; s < STAGES; ++s) {
        float y = d[s] * (sig - y1[s]) + x1[s];
        x1[s] = sig;
        y1[s] = y;
        sig = y;
    }
    return in * (1.0f - mix) + sig * mix;
}

float PhaserProcessor::process_channel(float in, float d, std::array<float, STAGES>& x1,
                                       std::array<float, STAGES>& y1, float& fb_state, float fb_amt,
                                       float mix) const {
    float sig = in + fb_amt * fb_state;

    for (size_t s = 0; s < STAGES; ++s) {
        float y = d * (sig - y1[s]) + x1[s];
        x1[s] = sig;
        y1[s] = y;
        sig = y;
    }

    fb_state = sig;
    return in * (1.0f - mix) + sig * mix;
}

void PhaserProcessor::process_block_mono(float* buf, size_t nframes) {
    const float depth = depth_pct.load() / 100.0f;
    const float fb_amt = feedback_pct.load() / 100.0f;
    const float mix = mix_pct.load() / 100.0f;
    const float ph_inc = rate_hz.load() / static_cast<float>(sample_rate);

    const bool current_vibe = vibe.load(std::memory_order_relaxed);
    float d = 0.0f;
    std::array<float, STAGES> dv{};
    for (size_t i = 0; i < nframes; ++i) {
        if (i % CTRL_INTERVAL == 0) {
            if (current_vibe) {
                vibe_coeffs(lfo_phase, depth, dv);
            } else {
                d = coeff_at(lfo_phase, depth);
            }
        }
        buf[i] = current_vibe ? process_vibe(buf[i], dv, x1_l, y1_l, mix)
                              : process_channel(buf[i], d, x1_l, y1_l, fb_state_l, fb_amt, mix);
        lfo_phase += ph_inc;
        if (lfo_phase >= 1.0f) {
            lfo_phase -= 1.0f;
        }
    }
}

void PhaserProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    const float depth = depth_pct.load() / 100.0f;
    const float fb_amt = feedback_pct.load() / 100.0f;
    const float mix = mix_pct.load() / 100.0f;
    const float ph_inc = rate_hz.load() / static_cast<float>(sample_rate);

    const bool current_vibe = vibe.load(std::memory_order_relaxed);
    float d = 0.0f;
    std::array<float, STAGES> dv{};
    for (size_t i = 0; i < nframes; ++i) {
        // Same LFO phase for both channels: identical mono effect per channel.
        if (i % CTRL_INTERVAL == 0) {
            if (current_vibe) {
                vibe_coeffs(lfo_phase, depth, dv);
            } else {
                d = coeff_at(lfo_phase, depth);
            }
        }
        l[i] = current_vibe ? process_vibe(l[i], dv, x1_l, y1_l, mix)
                            : process_channel(l[i], d, x1_l, y1_l, fb_state_l, fb_amt, mix);
        r[i] = current_vibe ? process_vibe(r[i], dv, x1_r, y1_r, mix)
                            : process_channel(r[i], d, x1_r, y1_r, fb_state_r, fb_amt, mix);
        lfo_phase += ph_inc;
        if (lfo_phase >= 1.0f) {
            lfo_phase -= 1.0f;
        }
    }
}

void PhaserProcessor::process_block_stereo_out(float* l, float* r, size_t nframes) {
    const float depth = depth_pct.load() / 100.0f;
    const float fb_amt = feedback_pct.load() / 100.0f;
    const float mix = mix_pct.load() / 100.0f;
    const float ph_inc = rate_hz.load() / static_cast<float>(sample_rate);

    const bool current_vibe = vibe.load(std::memory_order_relaxed);
    float d_l = 0.0f, d_r = 0.0f;
    std::array<float, STAGES> dv_l{}, dv_r{};
    for (size_t i = 0; i < nframes; ++i) {
        if (i % CTRL_INTERVAL == 0) {
            if (current_vibe) {
                vibe_coeffs(lfo_phase, depth, dv_l);
                vibe_coeffs(lfo_phase + 0.5f, depth, dv_r);
            } else {
                d_l = coeff_at(lfo_phase, depth);
                d_r = coeff_at(lfo_phase + 0.5f, depth); // 180° apart
            }
        }
        l[i] = current_vibe ? process_vibe(l[i], dv_l, x1_l, y1_l, mix)
                            : process_channel(l[i], d_l, x1_l, y1_l, fb_state_l, fb_amt, mix);
        r[i] = current_vibe ? process_vibe(r[i], dv_r, x1_r, y1_r, mix)
                            : process_channel(r[i], d_r, x1_r, y1_r, fb_state_r, fb_amt, mix);
        lfo_phase += ph_inc;
        if (lfo_phase >= 1.0f) {
            lfo_phase -= 1.0f;
        }
    }
}
