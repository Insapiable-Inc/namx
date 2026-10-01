#pragma once

#include <atomic>

#include "DelayLine.h"
#include "EffectBase.h"

/// Chorus: LFO-modulated delay. Modes: Normal (one voice), Tri (three voices
/// 120° apart — lusher), Vintage (triangle LFO, darker BBD-style wet),
/// Spatial (stereo: 90° LFOs, right wet phase-inverted for extra width).
/// Stereo mode uses a 180° offset (90° in Spatial).
class ChorusProcessor : public EffectBase
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
    void set_mix_pct(float pct) {
        m_mix_pct.store(pct);
    }
    enum Mode
    {
        NORMAL = 0,
        TRI,
        VINTAGE,
        SPATIAL
    };
    void set_mode(int m) {
        m_mode.store(m);
    }

    float get_rate_hz() const {
        return m_rate_hz.load();
    }
    float get_depth_pct() const {
        return m_depth_pct.load();
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
    std::atomic<float> m_rate_hz{1.0f};
    std::atomic<float> m_depth_pct{50.0f};
    std::atomic<float> m_mix_pct{50.0f};

    float m_lfo_phase{0.0f};
    std::atomic<int> m_mode{NORMAL};
    float m_vint_lp[2]{}; // Vintage wet low-pass per channel

    // Wet sample for one channel at an LFO phase (all modes).
    float wet(const DelayLine& line, float phase, float depth, float fs, int current_mode);

    DelayLine m_line_l, m_line_r;

    static constexpr float K_BASE_MS = 20.0f;
    static constexpr float K_MAX_DEPTH_MS = 5.0f;
};
