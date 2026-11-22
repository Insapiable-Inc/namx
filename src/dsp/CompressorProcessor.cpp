#include "CompressorProcessor.h"

#include <algorithm>
#include <cmath>

#include "Common.h"

namespace namx
{

static constexpr float DB_PER_OCTAVE = 6.0205999f; // 20·log10(2)

void CompressorProcessor::sync_right_channel() {
    env[1] = env[0];
    tone_state[1] = tone_state[0];
}

void CompressorProcessor::reset() {
    env[0] = env[1] = 0.0f;
    tone_state[0].reset();
    tone_state[1].reset();
}

void CompressorProcessor::process_channel(float* buf, size_t nframes, int ch) {
    const float fs = static_cast<float>(sample_rate);
    const float tone_knob = std::clamp(tone_pct.load(), 0.0f, 100.0f) / 100.0f;
    if (tone_knob != tone_fraction) { // redesign only when the knob moved (allocation-free)
        tone_fraction = tone_knob;
        tone.design(cs3::tone_chain(tone_knob), sample_rate);
    }
    const float t_ms =
        std::clamp(release_ms.load(), cs3::RELEASE_MS.back(), cs3::RELEASE_MS.front());
    const float knob = cs3::attack_fraction(t_ms);
    const float a_att = 1.0f - std::exp(-1000.0f / (fs * cs3::knob_map(cs3::ATTACK_TAU_MS, knob)));
    const float threshold = std::clamp(threshold_db.load() - cs3::REFERENCE_DBFS,
                                       cs3::THRESHOLD_LOW_DB, cs3::THRESHOLD_HIGH_DB);
    // Levels below are re REFERENCE_DBFS. The most gain there is rises a little as the release
    // gets faster.
    const float sustain_db =
        cs3::CEILING_DB - threshold + cs3::GAIN_DB_PER_E * std::log(cs3::RELEASE_MS[2] / t_ms);
    const float makeup = makeup_db.load() + cs3::knob_map(cs3::TONE_GAIN_DB, tone_knob);
    // The release (dBFS): toward a fixed floor, below the knee at any Sustain.
    const float q_rel = std::exp(-1000.0f / (fs * t_ms));
    const float floor_dbfs = cs3::FLOOR_DB + cs3::REFERENCE_DBFS;
    const float floor = std::exp2(floor_dbfs * (1.0f / DB_PER_OCTAVE));
    float& e = env[ch];
    float most_reduction = 0.0f;
    for (size_t i = 0; i < nframes; ++i) {
        const float rectified = std::abs(buf[i]);
        if (const float u = std::max(rectified, floor); u > e) {
            e += a_att * (u - e);
        } else {
            const float released = floor_dbfs + q_rel * (DB_PER_OCTAVE * std::log2(e) - floor_dbfs);
            e = std::max(u, std::exp2(released * (1.0f / DB_PER_OCTAVE)));
        }
        const float env_db =
            e > 1e-9f ? DB_PER_OCTAVE * std::log2(e) - cs3::REFERENCE_DBFS : -180.0f;
        const float over = std::max(0.0f, env_db + sustain_db - cs3::CEILING_DB);
        const float reduction = (1.0f - cs3::SLOPE) * over;
        most_reduction = std::max(most_reduction, reduction);
        const float gain = std::exp2((sustain_db - reduction + makeup) * (1.0f / DB_PER_OCTAVE));
        buf[i] = tone_state[ch].process(buf[i] * gain, tone);
    }
    if (most_reduction > gr_peak_db.load(std::memory_order_relaxed)) {
        gr_peak_db.store(most_reduction, std::memory_order_relaxed);
    }
}

void CompressorProcessor::process_block_mono(float* buf, size_t nframes) {
    process_channel(buf, nframes, 0);
}

void CompressorProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    process_channel(l, nframes, 0);
    process_channel(r, nframes, 1);
}

} // namespace namx
