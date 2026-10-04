#include "ReverbProcessor.h"

#include <algorithm>
#include <cmath>

#include "Common.h"

namespace namx
{

ReverbProcessor::ReverbProcessor() {
    on_sample_rate_changed(sample_rate);
}

void ReverbProcessor::on_sample_rate_changed(double sr) {
    shift.prepare(sr, 12.0f);
    for (auto& p : pre) {
        p.resize(static_cast<size_t>(sr * MAX_PRE_DELAY_MS / 1000.0) + 2);
    }
    plate.prepare(sr);
    hall.prepare(sr, reverb_tanks::Fdn::HALL);
    room.prepare(sr, reverb_tanks::Fdn::ROOM);
    reset();
}

void ReverbProcessor::reset() {
    plate.reset();
    hall.reset();
    room.reset();
    for (int ch = 0; ch < 2; ++ch) {
        pre[ch].clear();
        hp_x[ch] = hp_y[ch] = lp[ch] = 0.0f;
    }
    shift.reset();
    shim_fb = 0.0f;
}

float ReverbProcessor::pre_delay(int ch, float x, size_t d) {
    // Always fed, so raising Pre-delay later reads recent audio, not whatever the
    // line held when it was last used (audit 2026-10-04, Fable item 15).
    DelayLine& line = pre[ch];
    if (line.empty()) {
        return x;
    }
    line.push(x);
    return d == 0 ? x : line.at(d);
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
struct Cuts
{
    float hp_a, lp_k;
    size_t pre; // samples
};
static Cuts cuts_for(double sr, float low_hz, float high_hz, float pre_ms, float max_pre_ms) {
    constexpr float TWO_PI = 6.28318530718f;
    const float fs = static_cast<float>(sr);
    Cuts c;
    c.hp_a = std::exp(-TWO_PI * std::clamp(low_hz, 20.0f, 2000.0f) / fs);
    c.lp_k = 1.0f - std::exp(-TWO_PI * std::clamp(high_hz, 500.0f, 0.45f * fs) / fs);
    c.pre = static_cast<size_t>(std::clamp(pre_ms, 0.0f, max_pre_ms) * fs / 1000.0f);
    return c;
}

void ReverbProcessor::configure_tank(int t) {
    const float rt60 = decay_seconds(room_size_pct.load());
    const float damping = damping_pct.load() / 100.0f;
    if (t != active_type) {
        active_type = t; // a fresh tank: no stale tail from the last time it ran
        plate.reset();
        hall.reset();
        room.reset();
    }
    switch (static_cast<Type>(t)) {
        case Type::PLATE:
            plate.set_decay(rt60, damping);
            break;
        case Type::HALL:
            hall.set_decay(rt60, damping);
            break;
        case Type::ROOM:
            room.set_decay(rt60, damping);
            break;
    }
}

void ReverbProcessor::tank(int t, float in_l, float in_r, float& out_l, float& out_r) {
    float trim = 1.0f;
    switch (static_cast<Type>(t)) {
        case Type::PLATE:
            plate.process(in_l, in_r, out_l, out_r);
            trim = PLATE_TRIM;
            break;
        case Type::HALL:
            hall.process(in_l, in_r, out_l, out_r);
            trim = HALL_TRIM;
            break;
        case Type::ROOM:
            room.process(in_l, in_r, out_l, out_r);
            trim = ROOM_TRIM;
            break;
    }
    out_l *= trim;
    out_r *= trim;
}

void ReverbProcessor::run(float* l, float* r, size_t nframes, bool stereo_in, bool stereo_out) {
    const int t = type.load(std::memory_order_relaxed);
    configure_tank(t);
    const float width = width_pct.load() / 100.0f;
    const float mix = mix_pct.load() / 100.0f;
    const Cuts cut = cuts_for(sample_rate, low_cut_hz.load(), high_cut_hz.load(),
                              pre_delay_ms.load(), MAX_PRE_DELAY_MS);
    const float shim = std::clamp(shimmer_pct.load(), 0.0f, 100.0f) / 100.0f * 0.45f;
    // The shifter only runs while Shimmer is up: on the way back up it would replay
    // the tail it held when it was last on (audit 2026-10-04, Fable item 16).
    if (shim > 0.0f && !shim_on) {
        shift.reset();
        shim_fb = 0.0f;
    }
    shim_on = shim > 0.0f;
    const float wet1 = width * 0.5f + 0.5f, wet2 = (1.0f - width) * 0.5f;
    for (size_t i = 0; i < nframes; ++i) {
        const float dry_l = l[i];
        const float dry_r = stereo_in ? r[i] : l[i];
        const float in_l = pre_delay(0, dry_l, cut.pre) + shim * shim_fb;
        const float in_r = stereo_in ? pre_delay(1, dry_r, cut.pre) + shim * shim_fb : in_l;
        float wl = 0.0f, wr = 0.0f;
        tank(t, in_l, in_r, wl, wr);
        wl = wet_filter(0, wl, cut.hp_a, cut.lp_k);
        wr = wet_filter(1, wr, cut.hp_a, cut.lp_k);
        if (shim > 0.0f) {
            shim_fb = shift.process(0.5f * (wl + wr));
        }
        if (stereo_out) {
            l[i] = dry_l * (1.0f - mix) + (wl * wet1 + wr * wet2) * mix;
            r[i] = dry_r * (1.0f - mix) + (wr * wet1 + wl * wet2) * mix;
        } else {
            l[i] = dry_l * (1.0f - mix) + 0.5f * (wl + wr) * mix;
        }
    }
}

void ReverbProcessor::process_block_mono(float* buf, size_t nframes) {
    run(buf, buf, nframes, false, false);
}

void ReverbProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    run(l, r, nframes, true, true);
}

void ReverbProcessor::process_block_stereo_out(float* l, float* r, size_t nframes) {
    run(l, r, nframes, false, true);
}

} // namespace namx
