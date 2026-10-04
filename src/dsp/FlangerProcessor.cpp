#include "FlangerProcessor.h"

#include <algorithm>
#include <cmath>

#include "Common.h"

namespace namx
{

static constexpr float PI = 3.14159265358979323846f;

void FlangerProcessor::on_sample_rate_changed(double sr) {
    const size_t length = static_cast<size_t>((BASE_MS + DEPTH_MS + 5.0f) * sr / 1000.0f) + 4;
    line_l.resize(length);
    line_r.resize(length);
    reset();
}

void FlangerProcessor::sync_right_channel() {
    if (!line_l.empty()) {
        line_r.copy_from(line_l);
    }
    fb_r = fb_l;
}

void FlangerProcessor::reset() {
    line_l.clear();
    line_r.clear();
    fb_l = fb_r = 0.0f;
    lfo_phase = 0.0f;
}

void FlangerProcessor::process_sample(float in, float phase, DelayLine& line, float& fb,
                                      float depth, float fb_amt, float mix, float& out) const {
    float lfo = std::sin(2.0f * PI * phase);
    // Range scales the sweep centre and width together (max = BASE_MS/DEPTH_MS).
    const float r = std::clamp(range_pct.load(std::memory_order_relaxed), 0.0f, 100.0f) / 100.0f;
    const float base = 1.5f + (BASE_MS - 1.5f) * r;
    const float span = 1.5f + (DEPTH_MS - 1.5f) * r;
    float delay_sam =
        std::max(1.0f, (base + depth * span * lfo) * static_cast<float>(sample_rate) / 1000.0f);

    line.push(in + fb_amt * fb);
    const float wet = line.interpolate(delay_sam);
    fb = wet;

    out = in * (1.0f - mix) + wet * mix;
}

void FlangerProcessor::process_block_mono(float* buf, size_t nframes) {
    if (line_l.empty()) {
        return; // no sample rate yet (audit 2026-10-04, Fable item 17)
    }
    const float depth = depth_pct.load() / 100.0f;
    const float fb_amt = feedback_pct.load() / 100.0f;
    const float mix = mix_pct.load() / 100.0f;
    const float ph_inc = rate_hz.load() / static_cast<float>(sample_rate);

    for (size_t i = 0; i < nframes; ++i) {
        float out;
        process_sample(buf[i], lfo_phase, line_l, fb_l, depth, fb_amt, mix, out);
        buf[i] = out;

        lfo_phase += ph_inc;
        if (lfo_phase >= 1.0f) {
            lfo_phase -= 1.0f;
        }
    }
}

void FlangerProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    if (line_l.empty()) {
        return; // no sample rate yet (audit 2026-10-04, Fable item 17)
    }
    const float depth = depth_pct.load() / 100.0f;
    const float fb_amt = feedback_pct.load() / 100.0f;
    const float mix = mix_pct.load() / 100.0f;
    const float ph_inc = rate_hz.load() / static_cast<float>(sample_rate);

    for (size_t i = 0; i < nframes; ++i) {
        // Same LFO phase for both channels: identical mono effect per channel.
        float out_l, out_r;
        process_sample(l[i], lfo_phase, line_l, fb_l, depth, fb_amt, mix, out_l);
        process_sample(r[i], lfo_phase, line_r, fb_r, depth, fb_amt, mix, out_r);
        l[i] = out_l;
        r[i] = out_r;

        lfo_phase += ph_inc;
        if (lfo_phase >= 1.0f) {
            lfo_phase -= 1.0f;
        }
    }
}

void FlangerProcessor::process_block_stereo_out(float* l, float* r, size_t nframes) {
    if (line_l.empty()) {
        return; // no sample rate yet (audit 2026-10-04, Fable item 17)
    }
    const float depth = depth_pct.load() / 100.0f;
    const float fb_amt = feedback_pct.load() / 100.0f;
    const float mix = mix_pct.load() / 100.0f;
    const float ph_inc = rate_hz.load() / static_cast<float>(sample_rate);

    for (size_t i = 0; i < nframes; ++i) {
        float out_l, out_r;
        process_sample(l[i], lfo_phase, line_l, fb_l, depth, fb_amt, mix, out_l);
        process_sample(r[i], lfo_phase + 0.5f, line_r, fb_r, depth, fb_amt, mix, out_r);
        l[i] = out_l;
        r[i] = out_r;

        lfo_phase += ph_inc;
        if (lfo_phase >= 1.0f) {
            lfo_phase -= 1.0f;
        }
    }
}

} // namespace namx
