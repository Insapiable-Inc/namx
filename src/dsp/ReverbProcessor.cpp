#include "ReverbProcessor.h"

#include <algorithm>
#include <cmath>

constexpr int ReverbProcessor::COMB_LENGTHS[NUM_COMBS];
constexpr int ReverbProcessor::AP_LENGTHS[NUM_ALL_PASSES];

ReverbProcessor::ReverbProcessor() {
    allocate();
}

void ReverbProcessor::allocate() {
    double scale = sample_rate / 44100.0;
    for (auto& p : pre) {
        p.resize(static_cast<size_t>(sample_rate * MAX_PRE_DELAY_MS / 1000.0) + 2);
    }
    for (size_t i = 0; i < NUM_COMBS; ++i) {
        comb_l[i].resize(static_cast<size_t>(COMB_LENGTHS[i] * scale));
        comb_r[i].resize(static_cast<size_t>((COMB_LENGTHS[i] + STEREO_OFFSET) * scale));
    }
    for (size_t i = 0; i < NUM_ALL_PASSES; ++i) {
        ap_l[i].resize(static_cast<size_t>(AP_LENGTHS[i] * scale));
        ap_r[i].resize(static_cast<size_t>((AP_LENGTHS[i] + STEREO_OFFSET) * scale));
    }
}

void ReverbProcessor::on_sample_rate_changed(double sr) {
    for (auto& s : shift) {
        s.prepare(sr, 80.0f);
        s.set_semitones(12.0f);
    }
    allocate();
    reset();
}

void ReverbProcessor::reset() {
    for (auto& c : comb_l) {
        c.reset();
    }
    for (auto& c : comb_r) {
        c.reset();
    }
    for (auto& a : ap_l) {
        a.reset();
    }
    for (auto& a : ap_r) {
        a.reset();
    }
    for (int b = 0; b < 2; ++b) {
        pre[b].clear();
        hp_x[b] = hp_y[b] = lp[b] = 0.0f;
        shift[b].reset();
        shim_fb[b] = 0.0f;
    }
}

float ReverbProcessor::pre_delay(int bank, float x, size_t d) {
    DelayLine& line = pre[bank];
    if (d == 0 || line.empty()) {
        return x;
    }
    line.push(x);
    return line.at(d);
}

float ReverbProcessor::wet_filter(int ch, float x, float hp_a, float lp_k) {
    // One-pole high-pass then low-pass — gentle 6 dB/oct cuts on the tail.
    const float hp = hp_a * (hp_y[ch] + x - hp_x[ch]);
    hp_x[ch] = x;
    hp_y[ch] = hp;
    lp[ch] += lp_k * (hp - lp[ch]);
    return lp[ch];
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
    constexpr float TWO_PI = 6.28318530718f;
    const float fs = static_cast<float>(sr);
    Cuts c;
    c.hp_a = std::exp(-TWO_PI * std::clamp(low_hz, 20.0f, 2000.0f) / fs);
    c.lp_k = 1.0f - std::exp(-TWO_PI * std::clamp(high_hz, 500.0f, 0.45f * fs) / fs);
    c.pre = static_cast<size_t>(std::clamp(pre_ms, 0.0f, max_pre_ms) * fs / 1000.0f);
    return c;
}
} // namespace

void ReverbProcessor::process_block_stereo_out(float* l, float* r, size_t nframes) {
    const float room_size = room_size_pct.load() / 100.0f;
    const float damping = damping_pct.load() / 100.0f;
    const float width = width_pct.load() / 100.0f;
    const float mix = mix_pct.load() / 100.0f;
    const float feedback = 0.28f + room_size * 0.7f;
    const float damp = damping * 0.4f;
    const Cuts cut = cuts_for(sample_rate, low_cut_hz.load(), high_cut_hz.load(),
                              pre_delay_ms.load(), MAX_PRE_DELAY_MS);

    // Shimmer loop gain stays well under 1 — the comb feedback adds its own.
    const float shim = std::clamp(shimmer_pct.load(), 0.0f, 100.0f) / 100.0f * 0.45f;

    for (size_t i = 0; i < nframes; ++i) {
        float in_mono = pre_delay(0, (l[i] + r[i]) * 0.015f, cut.pre) + shim * shim_fb[0];

        float out_l = 0.0f, out_r = 0.0f;
        for (size_t c = 0; c < NUM_COMBS; ++c) {
            out_l += comb_l[c].process(in_mono, feedback, damp);
            out_r += comb_r[c].process(in_mono, feedback, damp);
        }
        for (size_t a = 0; a < NUM_ALL_PASSES; ++a) {
            out_l = ap_l[a].process(out_l);
            out_r = ap_r[a].process(out_r);
        }

        out_l = wet_filter(0, out_l, cut.hp_a, cut.lp_k);
        out_r = wet_filter(1, out_r, cut.hp_a, cut.lp_k);
        if (shim > 0.0f) {
            shim_fb[0] = shift[0].process(0.5f * (out_l + out_r) * 0.015f);
        }

        float wet1 = width * 0.5f + 0.5f;
        float wet2 = (1.0f - width) * 0.5f;
        l[i] = l[i] * (1.0f - mix) + (out_l * wet1 + out_r * wet2) * mix;
        r[i] = r[i] * (1.0f - mix) + (out_r * wet1 + out_l * wet2) * mix;
    }
}

void ReverbProcessor::process_mono_bank(float* buf, size_t nframes, int bank,
                                        std::array<CombFilter, NUM_COMBS>& combs,
                                        std::array<AllPassFilter, NUM_ALL_PASSES>& aps) {
    const float room_size = room_size_pct.load() / 100.0f;
    const float damping = damping_pct.load() / 100.0f;
    const float mix = mix_pct.load() / 100.0f;
    const float feedback = 0.28f + room_size * 0.7f;
    const float damp = damping * 0.4f;
    const Cuts cut = cuts_for(sample_rate, low_cut_hz.load(), high_cut_hz.load(),
                              pre_delay_ms.load(), MAX_PRE_DELAY_MS);

    const float shim = std::clamp(shimmer_pct.load(), 0.0f, 100.0f) / 100.0f * 0.45f;

    for (size_t i = 0; i < nframes; ++i) {
        float in_sig = pre_delay(bank, buf[i] * 0.015f, cut.pre) + shim * shim_fb[bank];
        float out = 0.0f;
        for (CombFilter& comb : combs) {
            out += comb.process(in_sig, feedback, damp);
        }
        for (AllPassFilter& ap : aps) {
            out = ap.process(out);
        }
        out = wet_filter(bank, out, cut.hp_a, cut.lp_k);
        if (shim > 0.0f) {
            shim_fb[bank] = shift[bank].process(out * 0.015f);
        }
        buf[i] = buf[i] * (1.0f - mix) + out * mix;
    }
}

void ReverbProcessor::process_block_mono(float* buf, size_t nframes) {
    process_mono_bank(buf, nframes, 0, comb_l, ap_l);
}

void ReverbProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    // Stereo input: independent mono reverb per channel, each on its own
    // comb/all-pass bank so the tails don't share state.
    process_mono_bank(l, nframes, 0, comb_l, ap_l);
    process_mono_bank(r, nframes, 1, comb_r, ap_r);
}
