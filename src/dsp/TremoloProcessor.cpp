#include "TremoloProcessor.h"

#include "Common.h"

namespace namx
{

TremoloProcessor::Block TremoloProcessor::block_settings() const {
    const tr2::Voicing& m = tr2::SUPPLY;
    const float rate = rate_hz.load();
    const float depth = tr2::depth(m, depth_pct.load() / 100.0f);
    return {m,
            coefficients,
            rate / static_cast<float>(sample_rate),
            tr2::wave_gain(m, wave_pct.load() / 100.0f, rate),
            depth,
            tr2::top(m, depth)};
}

void TremoloProcessor::process_block_mono(float* buf, size_t nframes) {
    const Block b = block_settings();

    for (size_t i = 0; i < nframes; ++i) {
        const float g = follower_l.step(tr2::target(b.voicing, phase, b.wave_gain, b.depth), b.k);
        buf[i] = b.top * g * low_cut_l.process(buf[i], b.k);
        advance_phase(b.ph_inc);
    }
}

void TremoloProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    const Block b = block_settings();

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

void TremoloProcessor::process_block_stereo_out(float* l, float* r, size_t nframes) {
    const Block b = block_settings();

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
