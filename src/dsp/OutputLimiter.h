#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "Common.h"

namespace namx
{

// Stereo-linked look-ahead peak limiter on the output stage (after the
// output gain, before the metronome): 1 ms look-ahead, ceiling −0.3 dBFS,
// 80 ms release. When off it costs nothing and adds no latency; when on it
// delays the output by the 1 ms look-ahead.
class OutputLimiter
{
public:
    void set_sample_rate(double sr); // non-RT
    void set_enabled(bool on) {
        enabled.store(on);
    }
    bool is_enabled() const {
        return enabled.load();
    }

    // Deepest reduction since the last take (dB ≥ 0), for a meter.
    float take_reduction_db() {
        return gr_peak_db.exchange(0.0f);
    }

    void process(float* l, float* r, size_t n);

    // The look-ahead delay while on, in ms. Any thread.
    double latency_ms() const {
        return enabled.load() ? look_ms.load(std::memory_order_relaxed) : 0.0;
    }

private:
    atomic<bool> enabled{false};
    atomic<float> gr_peak_db{0.0f};
    vector<float> delay_l, delay_r; // look-ahead lines
    // The lowest gain any sample in the look-ahead needs, as a monotonic queue over
    // the last `look` samples (audit 2026-10-04 item 13: scanning the window per
    // sample grew with the square of the rate). Ring of (gain, sample index), gains
    // ascending from head to tail; the head is the window minimum.
    vector<float> q_gain;
    vector<uint64_t> q_at;
    size_t q_head = 0, q_count = 0;
    uint64_t sample_index = 0;
    size_t pos = 0;
    size_t look = 48; // look-ahead, samples
    atomic<double> look_ms{1.0};
    float gain = 1.0f;
    float release = 0.9997f;
    float attack = 0.2f;
    bool was_enabled = false;
};

} // namespace namx
