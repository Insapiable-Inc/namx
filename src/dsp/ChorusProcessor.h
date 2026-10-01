#pragma once

#include <atomic>

#include "Common.h"
#include "DelayLine.h"
#include "EffectBase.h"

namespace npl
{

// Chorus: LFO-modulated delay. Modes: Normal (one voice), Tri (three voices
// 120° apart — lusher), Vintage (triangle LFO, darker BBD-style wet),
// Spatial (stereo: 90° LFOs, right wet phase-inverted for extra width).
// Stereo mode uses a 180° offset (90° in Spatial).
class ChorusProcessor : public EffectBase
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
    void set_mix_pct(float pct) {
        mix_pct.store(pct);
    }
    enum Mode
    {
        NORMAL = 0,
        TRI,
        VINTAGE,
        SPATIAL
    };
    void set_mode(int m) {
        mode.store(m);
    }

    float get_rate_hz() const {
        return rate_hz.load();
    }
    float get_depth_pct() const {
        return depth_pct.load();
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
    atomic<float> rate_hz{1.0f};
    atomic<float> depth_pct{50.0f};
    atomic<float> mix_pct{50.0f};

    float lfo_phase{0.0f};
    atomic<int> mode{NORMAL};
    float vint_lp[2]{}; // Vintage wet low-pass per channel

    // Wet sample for one channel at an LFO phase (all modes).
    float wet(const DelayLine& line, float phase, float depth, float fs, int current_mode);

    DelayLine line_l, line_r;

    static constexpr float BASE_MS = 20.0f;
    static constexpr float MAX_DEPTH_MS = 5.0f;
};

} // namespace npl
