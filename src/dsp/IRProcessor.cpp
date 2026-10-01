#include "IRProcessor.h"

#include <algorithm>
#include <cmath>

static constexpr float PI = 3.14159265358979323846f;

IRProcessor::IRProcessor()
    : al(CHUNK)
    , ar(CHUNK)
    , bl(CHUNK)
    , br(CHUNK) {
}

void IRProcessor::on_sample_rate_changed(double sr) {
    slot_a.set_target_sample_rate(sr);
    slot_b.set_target_sample_rate(sr);
    designed_low = designed_high = -1.0f; // redesign at the new rate
}

void IRProcessor::reset() {
    slot_a.reset();
    slot_b.reset();
    for (int c = 0; c < 2; ++c) {
        hp[c].clear();
        lp[c].clear();
    }
}

void IRProcessor::design_cuts(float low_hz, float high_hz) {
    // RBJ Butterworth high-pass / low-pass (Q = 0.707).
    const float fs = static_cast<float>(sample_rate);
    auto design = [fs](float f, bool high, Biquad& b) {
        f = std::clamp(f, 10.0f, 0.45f * fs);
        const float w = 2.0f * PI * f / fs, cw = std::cos(w),
                    alpha = std::sin(w) / (2.0f * 0.7071f);
        const float a0 = 1.0f + alpha;
        const float k = high ? (1.0f + cw) / 2.0f : (1.0f - cw) / 2.0f;
        b.b0 = k / a0;
        b.b1 = (high ? -2.0f * k : 2.0f * k) / a0;
        b.b2 = k / a0;
        b.a1 = -2.0f * cw / a0;
        b.a2 = (1.0f - alpha) / a0;
    };
    for (int c = 0; c < 2; ++c) {
        design(low_hz, true, hp[c]);
        design(high_hz, false, lp[c]);
    }
    designed_low = low_hz;
    designed_high = high_hz;
}

bool IRProcessor::process(float* l, float* r, size_t n, bool input_is_stereo) {
    // Long blocks in chunks so the scratch never grows on the audio thread.
    bool stereo = input_is_stereo;
    for (size_t off = 0; off < n; off += CHUNK) {
        stereo = process_chunk(l + off, r + off, std::min(CHUNK, n - off), input_is_stereo);
    }
    return stereo;
}

bool IRProcessor::process_chunk(float* l, float* r, size_t n, bool input_is_stereo) {
    const bool on = enabled.load();
    const bool has_a = slot_a.is_loaded();
    const bool has_b = slot_b.is_loaded();

    // Both loaders run every block (counting blocks for RtSlot reclaim);
    // a bypassed cab or an empty slot leaves the scratch untouched.
    std::copy(l, l + n, al.data());
    std::copy(r, r + n, ar.data());
    std::copy(l, l + n, bl.data());
    std::copy(r, r + n, br.data());
    slot_a.process_block(al.data(), ar.data(), n, input_is_stereo, on && has_a);
    slot_b.process_block(bl.data(), br.data(), n, input_is_stereo, on && has_b);
    if (!on || (!has_a && !has_b)) {
        return input_is_stereo;
    }

    // Per-slot gains: level × blend share × pan (constant power, unity at
    // centre) × phase. A single loaded slot plays at its own level only.
    const float blend = std::clamp(blend_pct.load(), 0.0f, 100.0f) / 100.0f;
    float g_l[2], g_r[2];
    bool panned = false;
    for (int s = 0; s < 2; ++s) {
        const bool loaded = s == 0 ? has_a : has_b;
        // Linear crossfade: two similar cabs at 50 % sum to the same level.
        const float share = !loaded             ? 0.0f
                            : !(has_a && has_b) ? 1.0f
                                                : (s == 0 ? 1.0f - blend : blend);
        const float lvl =
            std::pow(10.0f, level_db[s].load() / 20.0f) * share * (invert[s].load() ? -1.0f : 1.0f);
        const float pan = std::clamp(pan_pct[s].load(), -100.0f, 100.0f) / 100.0f;
        const float ang = (pan + 1.0f) * 0.25f * PI;
        g_l[s] = lvl * std::cos(ang) * std::sqrt(2.0f);
        g_r[s] = lvl * std::sin(ang) * std::sqrt(2.0f);
        if (loaded && std::abs(pan) > 0.001f) {
            panned = true;
        }
    }
    const bool stereo = input_is_stereo || panned;

    const float lo = low_cut_hz.load(), hi = high_cut_hz.load();
    if (lo != designed_low || hi != designed_high) {
        design_cuts(lo, hi);
    }
    const bool do_lo = lo > 20.5f, do_hi = hi < 19999.0f;

    for (size_t i = 0; i < n; ++i) {
        float sample_l = g_l[0] * al[i] + g_l[1] * bl[i];
        float sample_r =
            g_r[0] * (input_is_stereo ? ar[i] : al[i]) + g_r[1] * (input_is_stereo ? br[i] : bl[i]);
        if (!stereo) {
            sample_r = sample_l;
        }
        if (do_lo) {
            sample_l = hp[0].process(sample_l);
            if (stereo) {
                sample_r = hp[1].process(sample_r);
            }
        }
        if (do_hi) {
            sample_l = lp[0].process(sample_l);
            if (stereo) {
                sample_r = lp[1].process(sample_r);
            }
        }
        l[i] = sample_l;
        r[i] = stereo ? sample_r : sample_l;
    }
    return stereo;
}
