#include "NoiseGateProcessor.h"

#include <algorithm>
#include <cmath>

#include "Common.h"

namespace namx
{

static constexpr float DB_PER_OCTAVE = 6.0205999f; // 20·log10(2)

void NoiseGateProcessor::sync_right_channel() {
    envelope[1] = envelope[0];
    gain_per_channel[1] = gain_per_channel[0];
    ns2_state[1] = ns2_state[0];
}

void NoiseGateProcessor::reset() {
    envelope[0] = envelope[1] = 0.0f;
    gain_per_channel[0] = gain_per_channel[1] = 0.0f;
    ns2_state[0] = ns2_state[1] = Ns2State{};
}

// A type change starts the new engine from rest, like a reorder.
void NoiseGateProcessor::type_edge(int current_type) {
    if (current_type != last_type) {
        reset();
        last_type = current_type;
    }
}

void NoiseGateProcessor::ns2_channel(float* buf, size_t nframes, int ch) {
    const float fs = static_cast<float>(sample_rate);
    const float threshold = ns2::threshold_for(threshold_db.load());
    const float decay_ms = ns2::decay_for(release_ms.load());
    const auto falling = [fs](float ms) {
        return 1.0f - std::exp(-1.0f / (fs * ms * 0.001f));
    };
    const float a_fast = falling(decay_ms);
    const float a_slow = falling(decay_ms * ns2::RATIO);
    const float a_close = falling(decay_ms * ns2::CLOSE);
    const float peak_fall = std::exp(-1.0f / (fs * ns2::PEAK_RELEASE_MS * 0.001f));
    const float opens_at = threshold + ns2::OPEN_DB;
    const float closes_at = opens_at - ns2::HYSTERESIS_DB;

    Ns2State& s = ns2_state[ch];
    float deepest_db = 0.0f;
    for (size_t i = 0; i < nframes; ++i) {
        s.peak = std::max(std::abs(buf[i]), s.peak * peak_fall);
        // 20·log10(x) = (20·log10 2)·log2(x); log2/exp2 are cheaper per sample
        const float level = s.peak > 1e-7f ? DB_PER_OCTAVE * std::log2(s.peak) - ns2::REFERENCE_DBFS
                                           : ns2::FLOOR_DB;
        s.fast_db = level > s.fast_db ? level : s.fast_db + (level - s.fast_db) * a_fast;
        s.slow_db = level > s.slow_db ? level : s.slow_db + (level - s.slow_db) * a_slow;
        if (s.fast_db > opens_at) {
            s.is_open = true;
        } else if (s.fast_db < closes_at) {
            s.is_open = false;
        }
        s.switch_db = s.is_open ? 0.0f : s.switch_db + (-ns2::JUMP_DB - s.switch_db) * a_close;
        const float reduction_db = s.switch_db - ns2::SLOPE * std::max(0.0f, threshold - s.slow_db);
        deepest_db = std::min(deepest_db, reduction_db);
        buf[i] *= std::exp2((ns2::OPEN_GAIN_DB + std::max(reduction_db, ns2::FLOOR_DB)) *
                            (1.0f / DB_PER_OCTAVE));
    }
    const float gr_db = std::min(-deepest_db, MAX_GR_DB);
    if (gr_db > gr_peak_db.load(std::memory_order_relaxed)) {
        gr_peak_db.store(gr_db, std::memory_order_relaxed);
    }
}

void NoiseGateProcessor::process_channel(float* buf, size_t nframes, int ch) {
    const float thresh_lin = std::pow(10.0f, threshold_db.load() / 20.0f);
    const float fs = static_cast<float>(sample_rate);
    const float at_ms = attack_ms.load();
    const float rel_ms = release_ms.load();

    const float at_coeff = std::exp(-1.0f / (fs * at_ms * 0.001f));
    const float rel_coeff = std::exp(-1.0f / (fs * rel_ms * 0.001f));

    float& env = envelope[ch];
    float& gain = gain_per_channel[ch];
    float min_gain = 1.0f;

    for (size_t i = 0; i < nframes; ++i) {
        float level = std::abs(buf[i]);
        if (level > env) {
            env = at_coeff * env + (1.0f - at_coeff) * level;
        } else {
            env = rel_coeff * env + (1.0f - rel_coeff) * level;
        }

        float target = (env >= thresh_lin) ? 1.0f : 0.0f;

        if (target > gain) {
            gain = at_coeff * gain + (1.0f - at_coeff) * target;
        } else {
            gain = rel_coeff * gain + (1.0f - rel_coeff) * target;
        }

        min_gain = std::min(min_gain, gain);
        buf[i] *= gain;
    }
    // One log per block, not per sample.
    const float gr_db = min_gain > 1e-3f ? -20.0f * std::log10(min_gain) : MAX_GR_DB;
    if (gr_db > gr_peak_db.load(std::memory_order_relaxed)) {
        gr_peak_db.store(std::min(gr_db, MAX_GR_DB), std::memory_order_relaxed);
    }
}

void NoiseGateProcessor::process_block_mono(float* buf, size_t nframes) {
    const int current_type = type.load();
    type_edge(current_type);
    if (current_type == NS2) {
        ns2_channel(buf, nframes, 0);
    } else {
        process_channel(buf, nframes, 0);
    }
}

void NoiseGateProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    const int current_type = type.load();
    type_edge(current_type);
    if (current_type == NS2) {
        ns2_channel(l, nframes, 0);
        ns2_channel(r, nframes, 1);
    } else {
        process_channel(l, nframes, 0);
        process_channel(r, nframes, 1);
    }
}

} // namespace namx
