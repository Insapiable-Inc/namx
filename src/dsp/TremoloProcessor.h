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
        m_phase = 0.0f;
    }

    void set_rate(float hz) {
        m_rate.store(hz);
    }
    void set_depth(float pct) {
        m_depth.store(pct);
    }

    float get_rate() const {
        return m_rate.load();
    }
    float get_depth() const {
        return m_depth.load();
    }

protected:
    void on_sample_rate_changed(double /*sr*/) override {
        reset();
    }
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;
    void process_block_stereo_out(float* l, float* r, size_t nframes) override;

private:
    std::atomic<float> m_rate{5.0f};
    std::atomic<float> m_depth{50.0f};

    float m_phase{0.0f};
};
