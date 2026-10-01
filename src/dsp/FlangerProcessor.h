#pragma once

#include <atomic>

#include "DelayLine.h"
#include "EffectBase.h"

/// Flanger: short modulated delay with feedback. Stereo uses 180° LFO offset.
class FlangerProcessor : public EffectBase
{
public:
    bool supports_stereo_out() const override {
        return true;
    }
    void reset() override;

    void set_rate(float hz) {
        m_rate.store(hz);
    }
    void set_depth(float pct) {
        m_depth.store(pct);
    }
    void set_feedback(float pct) {
        m_feedback.store(pct);
    }
    void set_mix(float pct) {
        m_mix.store(pct);
    }
    /// 0 = short, metallic sweep (≈0–3 ms) … 100 = long jet sweep (0–14 ms).
    void set_range(float pct) {
        m_range.store(pct);
    }

    float rate() const {
        return m_rate.load();
    }
    float depth() const {
        return m_depth.load();
    }
    float feedback() const {
        return m_feedback.load();
    }
    float mix() const {
        return m_mix.load();
    }

protected:
    void on_sample_rate_changed(double sr) override;
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;
    void process_block_stereo_out(float* l, float* r, size_t nframes) override;

private:
    std::atomic<float> m_rate{0.3f}; // matches kFlangerRateDefault
    std::atomic<float> m_depth{50.0f};
    std::atomic<float> m_feedback{0.0f};
    std::atomic<float> m_mix{50.0f};
    std::atomic<float> m_range{100.0f};

    float m_phase{0.0f};

    DelayLine m_line_l, m_line_r;
    float m_fb_l{0.0f}, m_fb_r{0.0f};

    static constexpr float K_BASE_MS = 7.0f;
    static constexpr float K_DEPTH_MS = 7.0f;

    void process_sample(float in, float phase, DelayLine& line, float& fb, float depth,
                        float fb_amt, float mix, float& out) const;
};
