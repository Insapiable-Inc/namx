#pragma once

#include <atomic>

#include "Common.h"
#include "DelayLine.h"
#include "EffectBase.h"

namespace npl
{

/// Flanger: short modulated delay with feedback. Stereo uses 180° LFO offset.
class FlangerProcessor : public EffectBase
{
public:
    bool supports_stereo_out() const override {
        return true;
    }
    void reset() override;

    void set_rate_hz(float hz) {
        rate_hz.store(hz);
    }
    void set_depth_pct(float pct) {
        depth_pct.store(pct);
    }
    void set_feedback_pct(float pct) {
        feedback_pct.store(pct);
    }
    void set_mix_pct(float pct) {
        mix_pct.store(pct);
    }
    /// 0 = short, metallic sweep (≈0–3 ms) … 100 = long jet sweep (0–14 ms).
    void set_range_pct(float pct) {
        range_pct.store(pct);
    }

    float get_rate_hz() const {
        return rate_hz.load();
    }
    float get_depth_pct() const {
        return depth_pct.load();
    }
    float get_feedback_pct() const {
        return feedback_pct.load();
    }
    float get_mix_pct() const {
        return mix_pct.load();
    }

protected:
    void on_sample_rate_changed(double sr) override;
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;
    void process_block_stereo_out(float* l, float* r, size_t nframes) override;

private:
    atomic<float> rate_hz{0.3f}; // matches FLANGER_RATE_DEFAULT
    atomic<float> depth_pct{50.0f};
    atomic<float> feedback_pct{0.0f};
    atomic<float> mix_pct{50.0f};
    atomic<float> range_pct{100.0f};

    float lfo_phase{0.0f};

    DelayLine line_l, line_r;
    float fb_l{0.0f}, fb_r{0.0f};

    static constexpr float BASE_MS = 7.0f;
    static constexpr float DEPTH_MS = 7.0f;

    void process_sample(float in, float phase, DelayLine& line, float& fb, float depth,
                        float fb_amt, float mix, float& out) const;
};

} // namespace npl
