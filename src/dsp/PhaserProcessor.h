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

    void set_rate_hz(float hz) {
        m_rate_hz.store(hz);
    }
    void set_depth_pct(float pct) {
        m_depth_pct.store(pct);
    }
    void set_feedback_pct(float pct) {
        m_feedback_pct.store(pct);
    }
    /// 0 = classic phaser, 1 = Vibe.
    void set_vibe(bool on) {
        m_vibe.store(on);
    }
    void set_mix_pct(float pct) {
        m_mix_pct.store(pct);
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
    void on_sample_rate_changed(double /*sr*/) override {
        reset();
    }
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;
    void process_block_stereo_out(float* l, float* r, size_t nframes) override;

private:
    std::atomic<float> m_rate_hz{1.0f};
    std::atomic<float> m_depth_pct{50.0f};
    std::atomic<float> m_feedback_pct{50.0f};
    std::atomic<float> m_mix_pct{50.0f};

    float m_lfo_phase{0.0f};

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
