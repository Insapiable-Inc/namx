#include "FlangerProcessor.h"

#include <algorithm>
#include <cmath>

static constexpr float K_PI = 3.14159265358979323846f;

void FlangerProcessor::on_sample_rate_changed(double sr) {
    const size_t length = static_cast<size_t>((K_BASE_MS + K_DEPTH_MS + 5.0f) * sr / 1000.0f) + 4;
    m_line_l.resize(length);
    m_line_r.resize(length);
    reset();
}

void FlangerProcessor::reset() {
    m_line_l.clear();
    m_line_r.clear();
    m_fb_l = m_fb_r = 0.0f;
    m_lfo_phase = 0.0f;
}

void FlangerProcessor::process_sample(float in, float phase, DelayLine& line, float& fb,
                                      float depth, float fb_amt, float mix, float& out) const {
    float lfo = std::sin(2.0f * K_PI * phase);
    // Range scales the sweep centre and width together (max = kBaseMs/kDepthMs).
    const float r = std::clamp(m_range_pct.load(std::memory_order_relaxed), 0.0f, 100.0f) / 100.0f;
    const float base = 1.5f + (K_BASE_MS - 1.5f) * r;
    const float span = 1.5f + (K_DEPTH_MS - 1.5f) * r;
    float delay_sam =
        std::max(1.0f, (base + depth * span * lfo) * static_cast<float>(m_sample_rate) / 1000.0f);

    line.push(in + fb_amt * fb);
    const float wet = line.interpolate(delay_sam);
    fb = wet;

    out = in * (1.0f - mix) + wet * mix;
}

void FlangerProcessor::process_block_mono(float* buf, size_t nframes) {
    const float depth = m_depth_pct.load() / 100.0f;
    const float fb_amt = m_feedback_pct.load() / 100.0f;
    const float mix = m_mix_pct.load() / 100.0f;
    const float ph_inc = m_rate_hz.load() / static_cast<float>(m_sample_rate);

    for (size_t i = 0; i < nframes; ++i) {
        float out;
        process_sample(buf[i], m_lfo_phase, m_line_l, m_fb_l, depth, fb_amt, mix, out);
        buf[i] = out;

        m_lfo_phase += ph_inc;
        if (m_lfo_phase >= 1.0f) {
            m_lfo_phase -= 1.0f;
        }
    }
}

void FlangerProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    const float depth = m_depth_pct.load() / 100.0f;
    const float fb_amt = m_feedback_pct.load() / 100.0f;
    const float mix = m_mix_pct.load() / 100.0f;
    const float ph_inc = m_rate_hz.load() / static_cast<float>(m_sample_rate);

    for (size_t i = 0; i < nframes; ++i) {
        // Same LFO phase for both channels: identical mono effect per channel.
        float out_l, out_r;
        process_sample(l[i], m_lfo_phase, m_line_l, m_fb_l, depth, fb_amt, mix, out_l);
        process_sample(r[i], m_lfo_phase, m_line_r, m_fb_r, depth, fb_amt, mix, out_r);
        l[i] = out_l;
        r[i] = out_r;

        m_lfo_phase += ph_inc;
        if (m_lfo_phase >= 1.0f) {
            m_lfo_phase -= 1.0f;
        }
    }
}

void FlangerProcessor::process_block_stereo_out(float* l, float* r, size_t nframes) {
    const float depth = m_depth_pct.load() / 100.0f;
    const float fb_amt = m_feedback_pct.load() / 100.0f;
    const float mix = m_mix_pct.load() / 100.0f;
    const float ph_inc = m_rate_hz.load() / static_cast<float>(m_sample_rate);

    for (size_t i = 0; i < nframes; ++i) {
        float out_l, out_r;
        process_sample(l[i], m_lfo_phase, m_line_l, m_fb_l, depth, fb_amt, mix, out_l);
        process_sample(r[i], m_lfo_phase + 0.5f, m_line_r, m_fb_r, depth, fb_amt, mix, out_r);
        l[i] = out_l;
        r[i] = out_r;

        m_lfo_phase += ph_inc;
        if (m_lfo_phase >= 1.0f) {
            m_lfo_phase -= 1.0f;
        }
    }
}
