#pragma once

#include <atomic>
#include <cstdint>

#include "DelayLine.h"
#include "EffectBase.h"

/// Double-tracking: from a mono signal, the right channel becomes a second
/// "take" — a copy delayed 3–20 ms whose delay drifts randomly (timing and
/// a little pitch), the left stays dry. A stereo input passes untouched:
/// there is nothing mono left to double.
class DoublerProcessor : public EffectBase
{
public:
    bool supports_stereo_out() const override {
        return true;
    }
    void reset() override;

    void set_spread(float ms) {
        m_spread_ms.store(ms);
    }
    void set_mix(float pct) {
        m_mix.store(pct);
    }

    bool process(float* l, float* r, size_t n, bool input_is_stereo) override;

protected:
    void on_sample_rate_changed(double sr) override;

private:
    std::atomic<float> m_spread_ms{10.0f};
    std::atomic<float> m_mix{100.0f};

    DelayLine m_line;
    float m_drift = 0.0f, m_drift_target = 0.0f; // −1 … 1
    int m_until_new_target = 0;
    uint32_t m_seed = 0x9e3779b9u;
};
