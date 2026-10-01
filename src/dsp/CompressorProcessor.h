#pragma once

#include <atomic>

#include "EffectBase.h"

/// Feedforward peak compressor.
class CompressorProcessor : public EffectBase
{
public:
    void reset() override;

    void set_threshold(float d_b) {
        m_threshold_db.store(d_b);
    }
    void set_ratio(float ratio) {
        m_ratio.store(ratio);
    }
    void set_attack(float ms) {
        m_attack_ms.store(ms);
    }
    void set_release(float ms) {
        m_release_ms.store(ms);
    }
    void set_makeup(float d_b) {
        m_makeup_db.store(d_b);
    }

    float threshold() const {
        return m_threshold_db.load();
    }
    float ratio() const {
        return m_ratio.load();
    }
    float attack() const {
        return m_attack_ms.load();
    }
    float release() const {
        return m_release_ms.load();
    }
    float makeup() const {
        return m_makeup_db.load();
    }

    /// Deepest gain reduction (dB, ≥ 0) since the last take — GUI meter.
    float take_gain_reduction_db() {
        return m_gr_peak_db.exchange(0.0f);
    }

protected:
    void on_sample_rate_changed(double /*sr*/) override {
        reset();
    }
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;

private:
    void process_channel(float* buf, size_t nframes, int ch);

    std::atomic<float> m_threshold_db{-20.0f};
    std::atomic<float> m_ratio{4.0f};
    std::atomic<float> m_attack_ms{10.0f};
    std::atomic<float> m_release_ms{100.0f};
    std::atomic<float> m_makeup_db{0.0f};

    // Per-channel state: [0] = mono/L, [1] = R (stereo input).
    float m_env_db[2]{-120.0f, -120.0f};
    float m_gain_db[2]{0.0f, 0.0f};
    std::atomic<float> m_gr_peak_db{0.0f};
};
