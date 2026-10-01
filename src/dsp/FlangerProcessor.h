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

    void set_rate_hz(float hz) {
        m_rate_hz.store(hz);
    }
    void set_depth_pct(float pct) {
        m_depth_pct.store(pct);
    }
    void set_feedback_pct(float pct) {
        m_feedback_pct.store(pct);
    }
    void set_mix_pct(float pct) {
        m_mix_pct.store(pct);
    }
    /// 0 = short, metallic sweep (≈0–3 ms) … 100 = long jet sweep (0–14 ms).
    void set_range_pct(float pct) {
        m_range_pct.store(pct);
    }

    float get_rate_hz() const {
        return m_rate_hz.load();
    }
    float get_depth_pct() const {
        return m_depth_pct.load();
    }
    float get_feedback_pct() const {
        return m_feedback_pct.load();
    }
    float get_mix_pct() const {
        return m_mix_pct.load();
    }

protected:
    void on_sample_rate_changed(double sr) override;
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;
    void process_block_stereo_out(float* l, float* r, size_t nframes) override;

private:
    std::atomic<float> m_rate_hz{0.3f}; // matches kFlangerRateDefault
    std::atomic<float> m_depth_pct{50.0f};
    std::atomic<float> m_feedback_pct{0.0f};
    std::atomic<float> m_mix_pct{50.0f};
    std::atomic<float> m_range_pct{100.0f};

    float m_lfo_phase{0.0f};

    DelayLine m_line_l, m_line_r;
    float m_fb_l{0.0f}, m_fb_r{0.0f};

    static constexpr float K_BASE_MS = 7.0f;
    static constexpr float K_DEPTH_MS = 7.0f;

    void process_sample(float in, float phase, DelayLine& line, float& fb, float depth,
                        float fb_amt, float mix, float& out) const;
};
