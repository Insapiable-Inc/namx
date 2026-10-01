#pragma once

#include <atomic>

#include "EffectBase.h"

/// Wah: a resonant SVF band-pass swept either by an envelope follower (Auto)
/// or by Position (Pedal mode — a MIDI expression pedal's natural target).
class DynamicWahProcessor : public EffectBase
{
public:
    void reset() override;

    void set_sensitivity(float pct) {
        m_sensitivity.store(pct);
    }
    void set_resonance(float pct) {
        m_resonance.store(pct);
    }
    void set_mix(float pct) {
        m_mix.store(pct);
    }
    void set_pedal_mode(bool on) {
        m_pedal.store(on);
    }
    void set_position(float pct) {
        m_position.store(pct);
    } // 0 heel … 100 toe

    float sensitivity() const {
        return m_sensitivity.load();
    }
    float resonance() const {
        return m_resonance.load();
    }
    float mix() const {
        return m_mix.load();
    }

protected:
    void on_sample_rate_changed(double sr) override;
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;

private:
    void process_channel(float* buf, size_t nframes, int ch);

    static constexpr float K_BASE_FREQ = 400.0f;
    static constexpr float K_PEAK_FREQ = 3000.0f;

    std::atomic<float> m_sensitivity{70.0f};
    std::atomic<float> m_resonance{60.0f};
    std::atomic<float> m_mix{100.0f};
    std::atomic<bool> m_pedal{false};
    std::atomic<float> m_position{50.0f};
    float m_pos_smooth[2]{0.5f, 0.5f}; // de-zippered pedal position per channel

    float m_env_attack{0.0f};
    float m_env_release{0.0f};

    // Per-channel state: [0] = mono/L, [1] = R (stereo input).
    float m_env[2]{0.0f, 0.0f};
    float m_svf_lp[2]{0.0f, 0.0f};
    float m_svf_bp[2]{0.0f, 0.0f};
};
