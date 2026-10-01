#include "ReverbProcessor.h"

#include <algorithm>
#include <cmath>

constexpr int ReverbProcessor::K_COMB_LENGTHS[K_NUM_COMBS];
constexpr int ReverbProcessor::K_AP_LENGTHS[K_NUM_ALL_PASSES];

ReverbProcessor::ReverbProcessor() {
    allocate();
}

void ReverbProcessor::allocate() {
    double scale = m_sample_rate / 44100.0;
    for (auto& p : m_pre) {
        p.resize(static_cast<size_t>(m_sample_rate * K_MAX_PRE_DELAY_MS / 1000.0) + 2);
    }
    for (size_t i = 0; i < K_NUM_COMBS; ++i) {
        m_comb_l[i].resize(static_cast<size_t>(K_COMB_LENGTHS[i] * scale));
        m_comb_r[i].resize(static_cast<size_t>((K_COMB_LENGTHS[i] + K_STEREO_OFFSET) * scale));
    }
    for (size_t i = 0; i < K_NUM_ALL_PASSES; ++i) {
        m_ap_l[i].resize(static_cast<size_t>(K_AP_LENGTHS[i] * scale));
        m_ap_r[i].resize(static_cast<size_t>((K_AP_LENGTHS[i] + K_STEREO_OFFSET) * scale));
    }
}

void ReverbProcessor::on_sample_rate_changed(double sr) {
    for (auto& s : m_shift) {
        s.prepare(sr, 80.0f);
        s.set_semitones(12.0f);
    }
    allocate();
    reset();
}

void ReverbProcessor::reset() {
    for (auto& c : m_comb_l) {
        c.reset();
    }
    for (auto& c : m_comb_r) {
        c.reset();
    }
    for (auto& a : m_ap_l) {
        a.reset();
    }
    for (auto& a : m_ap_r) {
        a.reset();
    }
    for (int b = 0; b < 2; ++b) {
        m_pre[b].clear();
        m_hp_x[b] = m_hp_y[b] = m_lp[b] = 0.0f;
        m_shift[b].reset();
        m_shim_fb[b] = 0.0f;
    }
}

float ReverbProcessor::pre_delay(int bank, float x, size_t d) {
    DelayLine& line = m_pre[bank];
    if (d == 0 || line.empty()) {
        return x;
    }
    line.push(x);
    return line.at(d);
}

float ReverbProcessor::wet_filter(int ch, float x, float hp_a, float lp_k) {
    // One-pole high-pass then low-pass — gentle 6 dB/oct cuts on the tail.
    const float hp = hp_a * (m_hp_y[ch] + x - m_hp_x[ch]);
    m_hp_x[ch] = x;
    m_hp_y[ch] = hp;
    m_lp[ch] += lp_k * (hp - m_lp[ch]);
    return m_lp[ch];
}

// Shared per-block parameters.
namespace
{
struct Cuts
{
    float hp_a, lp_k;
    size_t pre; // samples
};
Cuts cuts_for(double sr, float low_hz, float high_hz, float pre_ms, float max_pre_ms) {
    constexpr float K_TWO_PI = 6.28318530718f;
    const float fs = static_cast<float>(sr);
    Cuts c;
    c.hp_a = std::exp(-K_TWO_PI * std::clamp(low_hz, 20.0f, 2000.0f) / fs);
    c.lp_k = 1.0f - std::exp(-K_TWO_PI * std::clamp(high_hz, 500.0f, 0.45f * fs) / fs);
    c.pre = static_cast<size_t>(std::clamp(pre_ms, 0.0f, max_pre_ms) * fs / 1000.0f);
    return c;
}
} // namespace

void ReverbProcessor::process_block_stereo_out(float* l, float* r, size_t nframes) {
    const float room_size = m_room_size.load() / 100.0f;
    const float damping = m_damping.load() / 100.0f;
    const float width = m_width.load() / 100.0f;
    const float mix = m_mix.load() / 100.0f;
    const float feedback = 0.28f + room_size * 0.7f;
    const float damp = damping * 0.4f;
    const Cuts cut = cuts_for(m_sample_rate, m_low_cut_hz.load(), m_high_cut_hz.load(),
                              m_pre_delay_ms.load(), K_MAX_PRE_DELAY_MS);

    // Shimmer loop gain stays well under 1 — the comb feedback adds its own.
    const float shim = std::clamp(m_shimmer.load(), 0.0f, 100.0f) / 100.0f * 0.45f;

    for (size_t i = 0; i < nframes; ++i) {
        float in_mono = pre_delay(0, (l[i] + r[i]) * 0.015f, cut.pre) + shim * m_shim_fb[0];

        float out_l = 0.0f, out_r = 0.0f;
        for (size_t c = 0; c < K_NUM_COMBS; ++c) {
            out_l += m_comb_l[c].process(in_mono, feedback, damp);
            out_r += m_comb_r[c].process(in_mono, feedback, damp);
        }
        for (size_t a = 0; a < K_NUM_ALL_PASSES; ++a) {
            out_l = m_ap_l[a].process(out_l);
            out_r = m_ap_r[a].process(out_r);
        }

        out_l = wet_filter(0, out_l, cut.hp_a, cut.lp_k);
        out_r = wet_filter(1, out_r, cut.hp_a, cut.lp_k);
        if (shim > 0.0f) {
            m_shim_fb[0] = m_shift[0].process(0.5f * (out_l + out_r) * 0.015f);
        }

        float wet1 = width * 0.5f + 0.5f;
        float wet2 = (1.0f - width) * 0.5f;
        l[i] = l[i] * (1.0f - mix) + (out_l * wet1 + out_r * wet2) * mix;
        r[i] = r[i] * (1.0f - mix) + (out_r * wet1 + out_l * wet2) * mix;
    }
}

void ReverbProcessor::process_mono_bank(float* buf, size_t nframes, int bank,
                                        std::array<CombFilter, K_NUM_COMBS>& combs,
                                        std::array<AllPassFilter, K_NUM_ALL_PASSES>& aps) {
    const float room_size = m_room_size.load() / 100.0f;
    const float damping = m_damping.load() / 100.0f;
    const float mix = m_mix.load() / 100.0f;
    const float feedback = 0.28f + room_size * 0.7f;
    const float damp = damping * 0.4f;
    const Cuts cut = cuts_for(m_sample_rate, m_low_cut_hz.load(), m_high_cut_hz.load(),
                              m_pre_delay_ms.load(), K_MAX_PRE_DELAY_MS);

    const float shim = std::clamp(m_shimmer.load(), 0.0f, 100.0f) / 100.0f * 0.45f;

    for (size_t i = 0; i < nframes; ++i) {
        float in_sig = pre_delay(bank, buf[i] * 0.015f, cut.pre) + shim * m_shim_fb[bank];
        float out = 0.0f;
        for (CombFilter& comb : combs) {
            out += comb.process(in_sig, feedback, damp);
        }
        for (AllPassFilter& ap : aps) {
            out = ap.process(out);
        }
        out = wet_filter(bank, out, cut.hp_a, cut.lp_k);
        if (shim > 0.0f) {
            m_shim_fb[bank] = m_shift[bank].process(out * 0.015f);
        }
        buf[i] = buf[i] * (1.0f - mix) + out * mix;
    }
}

void ReverbProcessor::process_block_mono(float* buf, size_t nframes) {
    process_mono_bank(buf, nframes, 0, m_comb_l, m_ap_l);
}

void ReverbProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    // Stereo input: independent mono reverb per channel, each on its own
    // comb/all-pass bank so the tails don't share state.
    process_mono_bank(l, nframes, 0, m_comb_l, m_ap_l);
    process_mono_bank(r, nframes, 1, m_comb_r, m_ap_r);
}
