#include "TremoloProcessor.h"

#include <cmath>
#include <numbers>

#include "Common.h"

namespace namx
{

void TremoloProcessor::process_block_mono(float* buf, size_t nframes) {
    if (type.load() == TR2) {
        tr2_mono(buf, nframes);
        return;
    }
    const float rate = rate_hz.load();
    const float depth = depth_pct.load() / 100.0f;
    const float ph_inc = rate / static_cast<float>(sample_rate);

    for (size_t i = 0; i < nframes; ++i) {
        float lfo = std::sin(2.0f * std::numbers::pi_v<float> * phase);
        float gain = 1.0f - depth * (1.0f - lfo) * 0.5f;
        buf[i] *= gain;
        advance_phase(ph_inc);
    }
}

void TremoloProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    if (type.load() == TR2) {
        tr2_dual(l, r, nframes);
        return;
    }
    const float rate = rate_hz.load();
    const float depth = depth_pct.load() / 100.0f;
    const float ph_inc = rate / static_cast<float>(sample_rate);

    for (size_t i = 0; i < nframes; ++i) {
        // Same LFO gain on both channels: identical mono effect per channel.
        float lfo = std::sin(2.0f * std::numbers::pi_v<float> * phase);
        float gain = 1.0f - depth * (1.0f - lfo) * 0.5f;
        l[i] *= gain;
        r[i] *= gain;
        advance_phase(ph_inc);
    }
}

void TremoloProcessor::process_block_stereo_out(float* l, float* r, size_t nframes) {
    if (type.load() == TR2) {
        tr2_stereo_out(l, r, nframes);
        return;
    }
    const float rate = rate_hz.load();
    const float depth = depth_pct.load() / 100.0f;
    const float ph_inc = rate / static_cast<float>(sample_rate);

    for (size_t i = 0; i < nframes; ++i) {
        float lfo_l = std::sin(2.0f * std::numbers::pi_v<float> * phase);
        float lfo_r = std::sin(2.0f * std::numbers::pi_v<float> *
                               (phase + 0.5f)); // 180° apart: true auto-pan
        l[i] *= 1.0f - depth * (1.0f - lfo_l) * 0.5f;
        r[i] *= 1.0f - depth * (1.0f - lfo_r) * 0.5f;
        advance_phase(ph_inc);
    }
}

TremoloProcessor::Tr2Block TremoloProcessor::tr2_block() const {
    const tr2::Voicing& m = tr2::SUPPLY;
    const float depth_fraction = depth_pct.load() / 100.0f;
    return {m,
            tr2_coefficients,
            rate_hz.load() / static_cast<float>(sample_rate),
            tr2::knob_map(m.wave_gain, wave_pct.load() / 100.0f),
            tr2::knob_map(m.depth, depth_fraction),
            tr2::knob_map(m.top, depth_fraction)};
}

void TremoloProcessor::tr2_mono(float* buf, size_t nframes) {
    const Tr2Block b = tr2_block();

    for (size_t i = 0; i < nframes; ++i) {
        const float g = follower_l.step(tr2::target(b.voicing, phase, b.wave_gain, b.depth), b.k);
        buf[i] = b.top * g * low_cut_l.process(buf[i], b.k);
        advance_phase(b.ph_inc);
    }
}

void TremoloProcessor::tr2_dual(float* l, float* r, size_t nframes) {
    const Tr2Block b = tr2_block();

    for (size_t i = 0; i < nframes; ++i) {
        // One gain for both channels (an identical mono effect per channel); the low
        // cut keeps state per channel.
        const float gain =
            b.top * follower_l.step(tr2::target(b.voicing, phase, b.wave_gain, b.depth), b.k);
        l[i] = gain * low_cut_l.process(l[i], b.k);
        r[i] = gain * low_cut_r.process(r[i], b.k);
        advance_phase(b.ph_inc);
    }
    follower_r = follower_l;
}

void TremoloProcessor::tr2_stereo_out(float* l, float* r, size_t nframes) {
    const Tr2Block b = tr2_block();

    for (size_t i = 0; i < nframes; ++i) {
        // The same input on both sides: one low cut, two gains 180° apart (auto-pan).
        const float x = low_cut_l.process(l[i], b.k);
        float phase_r = phase + 0.5f;
        if (phase_r >= 1.0f) {
            phase_r -= 1.0f;
        }
        l[i] =
            b.top * follower_l.step(tr2::target(b.voicing, phase, b.wave_gain, b.depth), b.k) * x;
        r[i] =
            b.top * follower_r.step(tr2::target(b.voicing, phase_r, b.wave_gain, b.depth), b.k) * x;
        advance_phase(b.ph_inc);
    }
    low_cut_r = low_cut_l;
}

} // namespace namx
