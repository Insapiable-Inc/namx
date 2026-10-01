#pragma once

#include <atomic>
#include <vector>

/// Stereo-linked look-ahead peak limiter on the output stage (after the
/// output gain, before the metronome): 1 ms look-ahead, ceiling −0.3 dBFS,
/// 80 ms release. When off it costs nothing and adds no latency; when on it
/// delays the output by the 1 ms look-ahead.
class OutputLimiter
{
public:
    void set_sample_rate(double sr); // non-RT
    void set_enabled(bool on) {
        m_enabled.store(on);
    }
    bool is_enabled() const {
        return m_enabled.load();
    }

    /// Deepest reduction since the last take (dB ≥ 0), for a meter.
    float take_reduction_db() {
        return m_gr_peak_db.exchange(0.0f);
    }

    void process(float* l, float* r, size_t n);

private:
    std::atomic<bool> m_enabled{false};
    std::atomic<float> m_gr_peak_db{0.0f};
    std::vector<float> m_delay_l, m_delay_r,
        m_need_window; // look-ahead lines + required gain per slot
    size_t m_pos = 0;
    size_t m_look = 48; // look-ahead, samples
    float m_gain = 1.0f;
    float m_release = 0.9997f;
    float m_attack = 0.2f;
    bool m_was_enabled = false;
};
