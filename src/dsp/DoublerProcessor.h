#pragma once

#include <atomic>
#include <cstdint>

#include "Common.h"
#include "DelayLine.h"
#include "EffectBase.h"

namespace npl
{

// Double-tracking: from a mono signal, the right channel becomes a second
// "take" — a copy delayed 3–20 ms whose delay drifts randomly (timing and
// a little pitch), the left stays dry. A stereo input passes untouched:
// there is nothing mono left to double.
class DoublerProcessor : public EffectBase
{
public:
    bool supports_stereo_out() const override {
        return true;
    }
    void reset() override;

    void set_spread(float ms) {
        spread_ms.store(ms);
    }
    void set_mix_pct(float pct) {
        mix_pct.store(pct);
    }

    bool process(float* l, float* r, size_t n, bool input_is_stereo) override;

protected:
    void on_sample_rate_changed(double sr) override;

private:
    atomic<float> spread_ms{10.0f};
    atomic<float> mix_pct{100.0f};

    DelayLine line;
    float drift = 0.0f, drift_target = 0.0f; // −1 … 1
    int until_new_target = 0;
    uint32_t seed = 0x9e3779b9u;
};

} // namespace npl
