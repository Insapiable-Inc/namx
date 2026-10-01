#pragma once

#include <array>
#include <atomic>

#include "EffectBase.h"

/// 4-stage all-pass phaser. Stereo uses 180° LFO offset.
class PhaserProcessor : public EffectBase
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
    /// 0 = classic phaser, 1 = Vibe.
    void set_vibe(bool on) {
        m_vibe.store(on);
    }
    void set_mix(float pct) {
        m_mix.store(pct);
    }

    float get_rate() const {
        return m_rate.load();
    }
    float get_depth() const {
        return m_depth.load();
    }
    float get_feedback() const {
        return m_feedback.load();
    }
    float get_mix() const {
        return m_mix.load();
    }

protected:
    void on_sample_rate_changed(double /*sr*/) override {
        reset();
    }
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;
    void process_block_stereo_out(float* l, float* r, size_t nframes) override;

private:
    std::atomic<float> m_rate{1.0f};
    std::atomic<float> m_depth{50.0f};
    std::atomic<float> m_feedback{50.0f};
    std::atomic<float> m_mix{50.0f};

    float m_phase{0.0f};

    static constexpr size_t K_STAGES = 4;
    std::atomic<bool> m_vibe{false};

    std::array<float, K_STAGES> m_x1_l{}, m_y1_l{};
    std::array<float, K_STAGES> m_x1_r{}, m_y1_r{};
    float m_fb_state_l{0.0f}, m_fb_state_r{0.0f};

    static constexpr float K_MIN_FREQ = 200.0f;
    static constexpr float K_MAX_FREQ = 4000.0f;

    // All-pass coefficient for an LFO phase (sin + pow + tan). Evaluated at
    // control rate — every kCtrlInterval samples — not per sample.
    float coeff_at(float phase, float depth) const;
    // Vibe: four stages at staggered fixed ratios (the Uni-Vibe's mismatched
    // photocells), lamp-shaped LFO, no feedback.
    void vibe_coeffs(float phase, float depth, std::array<float, K_STAGES>& d) const;
    static constexpr int K_CTRL_INTERVAL = 16; // 0.33 ms @ 48 kHz; LFO ≤ 5 Hz

    float process_vibe(float in, const std::array<float, K_STAGES>& d,
                       std::array<float, K_STAGES>& x1, std::array<float, K_STAGES>& y1,
                       float mix) const;
    float process_channel(float in, float d, std::array<float, K_STAGES>& x1,
                          std::array<float, K_STAGES>& y1, float& fb_state, float fb_amt,
                          float mix) const;
};
