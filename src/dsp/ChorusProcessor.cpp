#include "ChorusProcessor.h"

#include <algorithm>
#include <cmath>

static constexpr float K_PI = 3.14159265358979323846f;

void ChorusProcessor::on_sample_rate_changed(double sr) {
    const size_t length =
        static_cast<size_t>((K_BASE_MS + K_MAX_DEPTH_MS + 5.0f) * sr / 1000.0f) + 4;
    m_line_l.resize(length);
    m_line_r.resize(length);
    reset();
}

void ChorusProcessor::reset() {
    m_line_l.clear();
    m_line_r.clear();
    m_phase = 0.0f;
    m_vint_lp[0] = m_vint_lp[1] = 0.0f;
}

float ChorusProcessor::wet(const DelayLine& line, float phase, float depth, float fs, int mode) {
    auto tap = [&](float ph) {
        float lfo;
        if (mode == VINTAGE) {
            const float f = ph - std::floor(ph);
            lfo = 4.0f * std::abs(f - 0.5f) - 1.0f; // triangle
        } else {
            lfo = std::sin(2.0f * K_PI * ph);
        }
        const float del = std::max(1.0f, (K_BASE_MS + depth * K_MAX_DEPTH_MS * lfo) * fs / 1000.0f);
        return line.interpolate(del);
    };
    if (mode == TRI) {
        return (tap(phase) + tap(phase + 1.0f / 3.0f) + tap(phase + 2.0f / 3.0f)) * (1.0f / 3.0f);
    }
    return tap(phase);
}

// One processing loop for every dispatch case: `stereoOut` = mono in, true
// stereo out (L/R LFOs offset); `dual` = stereo in, same LFO per channel.
static inline float vintage_tone(float x, float& lp, float k) {
    lp += k * (x - lp);
    return lp;
}

void ChorusProcessor::process_block_mono(float* buf, size_t nframes) {
    const float depth = m_depth.load() / 100.0f;
    const float mix = m_mix.load() / 100.0f;
    const float fs = static_cast<float>(m_sample_rate);
    const float ph_inc = m_rate.load() / fs;
    const int mode = m_mode.load(std::memory_order_relaxed);
    const float lp_k = 1.0f - std::exp(-2.0f * K_PI * 3200.0f / fs);

    for (size_t i = 0; i < nframes; ++i) {
        m_line_l.push(buf[i]);
        float w = wet(m_line_l, m_phase, depth, fs, mode);
        if (mode == VINTAGE) {
            w = vintage_tone(w, m_vint_lp[0], lp_k);
        }
        buf[i] = buf[i] * (1.0f - mix) + w * mix;
        m_phase += ph_inc;
        if (m_phase >= 1.0f) {
            m_phase -= 1.0f;
        }
    }
}

void ChorusProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    const float depth = m_depth.load() / 100.0f;
    const float mix = m_mix.load() / 100.0f;
    const float fs = static_cast<float>(m_sample_rate);
    const float ph_inc = m_rate.load() / fs;
    const int mode = m_mode.load(std::memory_order_relaxed);
    const float lp_k = 1.0f - std::exp(-2.0f * K_PI * 3200.0f / fs);

    for (size_t i = 0; i < nframes; ++i) {
        // Same LFO for both channels: identical mono effect applied per channel.
        m_line_l.push(l[i]);
        m_line_r.push(r[i]);
        float w_l = wet(m_line_l, m_phase, depth, fs, mode);
        float w_r = wet(m_line_r, m_phase, depth, fs, mode);
        if (mode == VINTAGE) {
            w_l = vintage_tone(w_l, m_vint_lp[0], lp_k);
            w_r = vintage_tone(w_r, m_vint_lp[1], lp_k);
        }
        l[i] = l[i] * (1.0f - mix) + w_l * mix;
        r[i] = r[i] * (1.0f - mix) + w_r * mix;
        m_phase += ph_inc;
        if (m_phase >= 1.0f) {
            m_phase -= 1.0f;
        }
    }
}

void ChorusProcessor::process_block_stereo_out(float* l, float* r, size_t nframes) {
    const float depth = m_depth.load() / 100.0f;
    const float mix = m_mix.load() / 100.0f;
    const float fs = static_cast<float>(m_sample_rate);
    const float ph_inc = m_rate.load() / fs;
    const int mode = m_mode.load(std::memory_order_relaxed);
    const float lp_k = 1.0f - std::exp(-2.0f * K_PI * 3200.0f / fs);
    const float off = mode == SPATIAL ? 0.25f : 0.5f; // LFO offset
    const float r_sign = mode == SPATIAL ? -1.0f : 1.0f; // Spatial: inverted right wet

    for (size_t i = 0; i < nframes; ++i) {
        m_line_l.push(l[i]);
        m_line_r.push(r[i]);
        float w_l = wet(m_line_l, m_phase, depth, fs, mode);
        float w_r = wet(m_line_r, m_phase + off, depth, fs, mode);
        if (mode == VINTAGE) {
            w_l = vintage_tone(w_l, m_vint_lp[0], lp_k);
            w_r = vintage_tone(w_r, m_vint_lp[1], lp_k);
        }
        l[i] = l[i] * (1.0f - mix) + w_l * mix;
        r[i] = r[i] * (1.0f - mix) + r_sign * w_r * mix;
        m_phase += ph_inc;
        if (m_phase >= 1.0f) {
            m_phase -= 1.0f;
        }
    }
}
