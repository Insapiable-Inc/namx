#include "CompressorProcessor.h"

#include <algorithm>
#include <cmath>

#include "Common.h"

namespace namx
{

static constexpr float DB_PER_OCTAVE = 6.0205999f; // 20·log10(2)

void CompressorProcessor::sync_right_channel() {
    env_db_per_channel[1] = env_db_per_channel[0];
    gain_db_per_channel[1] = gain_db_per_channel[0];
}

void CompressorProcessor::reset() {
    env_db_per_channel[0] = env_db_per_channel[1] = -120.0f;
    gain_db_per_channel[0] = gain_db_per_channel[1] = 0.0f;
}

void CompressorProcessor::process_channel(float* buf, size_t nframes, int ch) {
    const float threshold = threshold_db.load();
    const float current_ratio = ratio.load();
    const float makeup = makeup_db.load();
    const float fs = static_cast<float>(sample_rate);

    const float at_coeff = std::exp(-1.0f / (fs * attack_ms.load() * 0.001f));
    const float rel_coeff = std::exp(-1.0f / (fs * release_ms.load() * 0.001f));

    float& env_db = env_db_per_channel[ch];
    float& gain_db = gain_db_per_channel[ch];
    float min_gain_db = 0.0f;

    for (size_t i = 0; i < nframes; ++i) {
        // 20·log10(x) = (20·log10 2)·log2(x); log2/exp2 are much cheaper
        // than log10/pow per sample.
        float level = std::abs(buf[i]);
        float level_db = (level > 1e-9f) ? DB_PER_OCTAVE * std::log2(level) : -120.0f;

        if (level_db > env_db) {
            env_db = at_coeff * env_db + (1.0f - at_coeff) * level_db;
        } else {
            env_db = rel_coeff * env_db + (1.0f - rel_coeff) * level_db;
        }

        float target_gr = 0.0f;
        if (env_db > threshold) {
            target_gr = (threshold - env_db) * (1.0f - 1.0f / current_ratio);
        }

        if (target_gr < gain_db) {
            gain_db = at_coeff * gain_db + (1.0f - at_coeff) * target_gr;
        } else {
            gain_db = rel_coeff * gain_db + (1.0f - rel_coeff) * target_gr;
        }

        min_gain_db = std::min(min_gain_db, gain_db);
        buf[i] *= std::exp2((gain_db + makeup) * (1.0f / DB_PER_OCTAVE));
    }
    if (-min_gain_db > gr_peak_db.load(std::memory_order_relaxed)) {
        gr_peak_db.store(-min_gain_db, std::memory_order_relaxed);
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
