#pragma once

#include <atomic>

#include "EffectBase.h"

/// Tremolo: amplitude modulation. Stereo uses a 180° LFO offset (auto-pan, like the other
/// modulation effects).
class TremoloProcessor : public EffectBase
{
public:
    bool supports_stereo_out() const override {
        return true;
    }
    void reset() override {
        phase = 0.0f;
    }

    void set_rate_hz(float hz) {
        rate_hz.store(hz);
    }
    void set_depth_pct(float pct) {
        depth_pct.store(pct);
    }

    float get_rate_hz() const {
        return rate_hz.load();
    }
    float get_depth_pct() const {
        return depth_pct.load();
    }

protected:
    void on_sample_rate_changed(double /*sr*/) override {
        reset();
    }
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;
    void process_block_stereo_out(float* l, float* r, size_t nframes) override;

private:
    std::atomic<float> rate_hz{5.0f};
    std::atomic<float> depth_pct{50.0f};

    float phase{0.0f};
};
