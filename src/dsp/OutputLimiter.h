#pragma once

#include <atomic>
#include <vector>

#include "Common.h"

namespace npl
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

private:
    atomic<bool> enabled{false};
    atomic<float> gr_peak_db{0.0f};
    vector<float> delay_l, delay_r,
        need_window; // look-ahead lines + required gain per slot
    size_t pos = 0;
    size_t look = 48; // look-ahead, samples
    float gain = 1.0f;
    float release = 0.9997f;
    float attack = 0.2f;
    bool was_enabled = false;
};

} // namespace npl
