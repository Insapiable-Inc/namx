#include "ReverbProcessor.h"

#include <numbers>

#include "Common.h"

namespace namx
{

ReverbProcessor::ReverbProcessor() {
    on_sample_rate_changed(sample_rate);
}

void ReverbProcessor::on_sample_rate_changed(double sr) {
    rv6.prepare(sr);
    reset();
}

void ReverbProcessor::reset() {
    rv6.reset();
}

void ReverbProcessor::run(float* l, float* r, size_t nframes, bool stereo_in, bool stereo_out) {
    const int t = type.load(std::memory_order_relaxed);
    if (t != active_type) {
        active_type = t; // a fresh tank: no stale tail from the last mode
        rv6.reset();
    }
    const auto mode = static_cast<rv6::Mode>(t - FIRST_TYPE);
    rv6.set_controls(mode, room_size_pct.load() / 100.0f, damping_pct.load() / 100.0f,
                     mix_pct.load() / 100.0f);
    // A and B are decorrelated: a mono output sums them at equal power.
    constexpr float MONO = std::numbers::sqrt2_v<float> / 2.0f;
    for (size_t i = 0; i < nframes; ++i) {
        const float dry_l = l[i];
        const float dry_r = stereo_in ? r[i] : l[i];
        const float in = stereo_in ? 0.5f * (dry_l + dry_r) : dry_l;
        float a = 0.0f, b = 0.0f;
        rv6.process(in, a, b);
        if (stereo_out) {
            l[i] = dry_l + a;
            r[i] = dry_r + b;
        } else {
            l[i] = dry_l + MONO * (a + b);
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
