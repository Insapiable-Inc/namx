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

    void set_rate(float hz) {
        m_rate.store(hz);
    }
    void set_depth(float pct) {
        m_depth.store(pct);
    }
    void set_mix(float pct) {
        m_mix.store(pct);
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

    float rate() const {
        return m_rate.load();
    }
    float depth() const {
        return m_depth.load();
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
    std::atomic<float> m_rate{1.0f};
    std::atomic<float> m_depth{50.0f};
    std::atomic<float> m_mix{50.0f};

    float m_phase{0.0f};
    std::atomic<int> m_mode{NORMAL};
    float m_vint_lp[2]{}; // Vintage wet low-pass per channel

    // Wet sample for one channel at an LFO phase (all modes).
    float wet(const DelayLine& line, float phase, float depth, float fs, int mode);

    DelayLine m_line_l, m_line_r;

    static constexpr float K_BASE_MS = 20.0f;
    static constexpr float K_MAX_DEPTH_MS = 5.0f;
};
