#include "NoiseGateProcessor.h"

#include <algorithm>
#include <cmath>

#include "Common.h"

namespace namx
{

void NoiseGateProcessor::sync_right_channel() {
    control[1] = control[0];
    tone_state[1] = tone_state[0];
}

void NoiseGateProcessor::reset() {
    for (auto& c : control) {
        c.reset();
    }
    tone_state[0].reset();
    tone_state[1].reset();
}

void NoiseGateProcessor::process_channel(float* buf, size_t nframes, int ch) {
    if (sample_rate != prepared_rate) { // allocation-free, once per rate
        prepared_rate = sample_rate;
        tone.design(ns2::TONE, sample_rate);
        for (auto& c : control) {
            c.prepare(sample_rate);
        }
    }
    static const float REFERENCE = std::pow(10.0f, ns2::REFERENCE_DBFS / 20.0f);
    static const float OPEN_GAIN = std::pow(10.0f, ns2::OPEN_GAIN_DB / 20.0f);
    ns2::Control& c = control[ch];
    c.set(threshold_db.load(), release_ms.load());

    float least = 1.0f;
    for (size_t i = 0; i < nframes; ++i) {
        const float gain = c.process(buf[i] / REFERENCE);
        least = std::min(least, gain);
        buf[i] = tone_state[ch].process(buf[i] * (gain * OPEN_GAIN), tone);
    }
    const float gr_db = std::min(-20.0f * std::log10(least), MAX_GR_DB);
    if (gr_db > gr_peak_db.load(std::memory_order_relaxed)) {
        gr_peak_db.store(gr_db, std::memory_order_relaxed);
    }
}

void NoiseGateProcessor::process_block_mono(float* buf, size_t nframes) {
    process_channel(buf, nframes, 0);
}

void NoiseGateProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    process_channel(l, nframes, 0);
    process_channel(r, nframes, 1);
}

} // namespace namx
