#pragma once

#include <atomic>

#include "EffectBase.h"

/// Wah: a resonant SVF band-pass swept either by an envelope follower (Auto)
/// or by Position (Pedal mode — a MIDI expression pedal's natural target).
class DynamicWahProcessor : public EffectBase
{
public:
    void reset() override;

    void set_sensitivity_pct(float pct) {
        m_sensitivity_pct.store(pct);
    }
    void set_resonance_pct(float pct) {
        m_resonance_pct.store(pct);
    }
    void set_mix_pct(float pct) {
        m_mix_pct.store(pct);
    }
    void set_pedal_mode(bool on) {
        m_pedal.store(on);
    }
    void set_position_pct(float pct) {
        m_position_pct.store(pct);
    } // 0 heel … 100 toe

    float get_sensitivity_pct() const {
        return m_sensitivity_pct.load();
    }
    float get_resonance_pct() const {
        return m_resonance_pct.load();
    }
    float get_mix_pct() const {
        return m_mix_pct.load();
    }

protected:
    void on_sample_rate_changed(double sr) override;
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;

private:
    void process_channel(float* buf, size_t nframes, int ch);

    static constexpr float K_BASE_FREQ = 400.0f;
    static constexpr float K_PEAK_FREQ = 3000.0f;

    std::atomic<float> m_sensitivity_pct{70.0f};
    std::atomic<float> m_resonance_pct{60.0f};
    std::atomic<float> m_mix_pct{100.0f};
    std::atomic<bool> m_pedal{false};
    std::atomic<float> m_position_pct{50.0f};
    float m_pos_smooth[2]{0.5f, 0.5f}; // de-zippered pedal position per channel

    float m_env_attack{0.0f};
    float m_env_release{0.0f};

    // Per-channel state: [0] = mono/L, [1] = R (stereo input).
    float m_env_per_channel[2]{0.0f, 0.0f};
    float m_svf_lp[2]{0.0f, 0.0f};
    float m_svf_bp[2]{0.0f, 0.0f};
};
