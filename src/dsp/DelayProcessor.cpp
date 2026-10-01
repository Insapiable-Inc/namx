#include "DelayProcessor.h"

#include <algorithm>
#include <cmath>

static constexpr float K_TWO_PI = 6.28318530718f;

void DelayProcessor::on_sample_rate_changed(double sr) {
    const size_t n = static_cast<size_t>(K_MAX_DELAY_MS * sr / 1000.0) + 4;
    for (auto& l : m_lines) {
        l.samples.resize(n);
    }
    for (auto& s : m_shift) {
        s.prepare(sr, 60.0f);
        s.set_semitones(12.0f);
    }
    reset();
}

void DelayProcessor::reset() {
    for (auto& l : m_lines) {
        l.samples.clear();
        l.hp_x = l.hp_y = l.lp = 0.0f;
    }
    m_mod_phase = 0.0f;
    for (auto& s : m_shift) {
        s.reset();
    }
}

float DelayProcessor::loop(int line, float d, const Params& p) {
    if (p.crystals <= 0.0f) {
        return d;
    }
    return d + p.crystals * (m_shift[line].process(d) - d);
}

DelayProcessor::Params DelayProcessor::params() const {
    const float fs = static_cast<float>(m_sample_rate);
    Params p;
    p.fb = std::min(m_feedback_pct.load() / 100.0f, 0.99f);
    p.mix = m_mix_pct.load() / 100.0f;
    p.cross = std::clamp(m_cross_pct.load(), 0.0f, 100.0f) / 100.0f;
    p.hp_a = std::exp(-K_TWO_PI * std::clamp(m_low_cut_hz.load(), 20.0f, 2000.0f) / fs);
    p.lp_k = 1.0f - std::exp(-K_TWO_PI * std::clamp(m_high_cut_hz.load(), 500.0f, 0.45f * fs) / fs);
    p.sat_drive = std::clamp(m_sat_pct.load(), 0.0f, 100.0f) / 100.0f;
    p.mod_depth = 0.003f * fs * std::clamp(m_mod_pct.load(), 0.0f, 100.0f) / 100.0f; // up to ±3 ms
    p.ph_inc = 0.55f / fs; // 0.55 Hz wobble
    p.crystals = std::clamp(m_crystals_pct.load(), 0.0f, 100.0f) / 100.0f;
    const float max_d = static_cast<float>(m_lines[0].samples.size()) - 2.0f - p.mod_depth;
    p.del_l = std::clamp(m_time_ms.load() * fs / 1000.0f, 1.0f + p.mod_depth, max_d);
    p.del_r = std::clamp(p.del_l * std::clamp(m_right_pct.load(), 25.0f, 200.0f) / 100.0f,
                         1.0f + p.mod_depth, max_d);
    return p;
}

float DelayProcessor::tone(Line& l, float x, const Params& p) const {
    const float hp = p.hp_a * (l.hp_y + x - l.hp_x);
    l.hp_x = x;
    l.hp_y = hp;
    l.lp += p.lp_k * (hp - l.lp);
    return l.lp;
}

float DelayProcessor::saturate(float x, float amount) {
    if (amount <= 0.0f) {
        return x;
    }
    // Blend toward a soft clip (Padé tanh): unity small-signal gain, loud
    // repeats round off and compress — tape-like, never harsher.
    const float v = std::clamp(x, -3.0f, 3.0f);
    const float t = v * (27.0f + v * v) / (27.0f + 9.0f * v * v);
    return x + amount * (t - x);
}

void DelayProcessor::process_block_mono(float* buf, size_t nframes) {
    if (m_lines[0].samples.empty()) {
        return;
    }
    const Params p = params();
    Line& l = m_lines[0];
    for (size_t i = 0; i < nframes; ++i) {
        const float mod = p.mod_depth * std::sin(K_TWO_PI * m_mod_phase);
        m_mod_phase += p.ph_inc;
        if (m_mod_phase >= 1.0f) {
            m_mod_phase -= 1.0f;
        }

        const float d = tone(l, l.read(p.del_l + mod), p);
        l.push(saturate(buf[i] + p.fb * loop(0, d, p), p.sat_drive));
        buf[i] = buf[i] * (1.0f - p.mix) + d * p.mix;
    }
}

void DelayProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    // Stereo input: independent line per channel (no cross-feedback).
    if (m_lines[0].samples.empty()) {
        return;
    }
    const Params p = params();
    for (size_t i = 0; i < nframes; ++i) {
        const float mod = p.mod_depth * std::sin(K_TWO_PI * m_mod_phase);
        m_mod_phase += p.ph_inc;
        if (m_mod_phase >= 1.0f) {
            m_mod_phase -= 1.0f;
        }

        const float d_l = tone(m_lines[0], m_lines[0].read(p.del_l + mod), p);
        const float d_r = tone(m_lines[1], m_lines[1].read(p.del_r - mod), p);
        m_lines[0].push(saturate(l[i] + p.fb * loop(0, d_l, p), p.sat_drive));
        m_lines[1].push(saturate(r[i] + p.fb * loop(1, d_r, p), p.sat_drive));
        l[i] = l[i] * (1.0f - p.mix) + d_l * p.mix;
        r[i] = r[i] * (1.0f - p.mix) + d_r * p.mix;
    }
}

void DelayProcessor::process_block_stereo_out(float* l, float* r, size_t nframes) {
    // Cross-feed c: 1 = ping-pong (input → left only, repeats alternate),
    // 0 = two independent lines (input into both, L/R times).
    if (m_lines[0].samples.empty()) {
        return;
    }
    const Params p = params();
    const float c = p.cross;
    for (size_t i = 0; i < nframes; ++i) {
        const float mod = p.mod_depth * std::sin(K_TWO_PI * m_mod_phase);
        m_mod_phase += p.ph_inc;
        if (m_mod_phase >= 1.0f) {
            m_mod_phase -= 1.0f;
        }

        const float x = l[i];
        const float d_l = tone(m_lines[0], m_lines[0].read(p.del_l + mod), p);
        const float d_r = tone(m_lines[1], m_lines[1].read(p.del_r - mod), p);
        const float f_l = loop(0, d_l, p), f_r = loop(1, d_r, p);
        m_lines[0].push(saturate(x + p.fb * ((1.0f - c) * f_l + c * f_r), p.sat_drive));
        m_lines[1].push(
            saturate((1.0f - c) * x + p.fb * ((1.0f - c) * f_r + c * f_l), p.sat_drive));
        l[i] = x * (1.0f - p.mix) + d_l * p.mix;
        r[i] = r[i] * (1.0f - p.mix) + d_r * p.mix;
    }
}
