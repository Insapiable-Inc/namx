#pragma once

#include "Common.h"
#include "EffectBase.h"
#include "Phase90Circuit.h"

namespace namx
{

// The MXR Phase 90 (Phase90Circuit) with its LFO, plus Depth, Feedback and Mix. Stereo
// runs the right channel's LFO 180° behind.
class PhaserProcessor : public EffectBase
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
    void set_feedback_pct(float pct) {
        feedback_pct.store(pct);
    }
    void set_mix_pct(float pct) {
        mix_pct.store(pct);
    }

    float get_rate_hz() const {
        return rate_hz.load();
    }
    float get_depth_pct() const {
        return depth_pct.load();
    }
    float get_feedback_pct() const {
        return feedback_pct.load();
    }
    float get_mix_pct() const {
        return mix_pct.load();
    }

protected:
    void on_sample_rate_changed(double sr) override {
        circuit_l.prepare(sr);
        circuit_r.prepare(sr);
        reset();
    }
    void process_block_mono(float* buf, size_t nframes) override {
        render(buf, nullptr, nframes, false);
    }
    void process_block_dual(float* l, float* r, size_t nframes) override {
        render(l, r, nframes, false);
    }
    void process_block_stereo_out(float* l, float* r, size_t nframes) override {
        render(l, r, nframes, true);
    }

private:
    atomic<float> rate_hz{0.4f};
    atomic<float> depth_pct{50.0f};
    atomic<float> feedback_pct{0.0f};
    atomic<float> mix_pct{50.0f};

    float lfo_phase{0.0f};
    Phase90Circuit circuit_l, circuit_r;

    // r is null for mono; with stereo_lfo the right channel's LFO runs 180° behind.
    void render(float* l, float* r, size_t nframes, bool stereo_lfo);
    // The Phase 90's LFO as gate − source volts: an asymmetric triangle (rises for 65 %
    // of the period, as in the paper's reference), up from the sweep's low end; Depth
    // 50 % is the pedal's swing, 100 % twice it.
    static float gate_vgs(float phase, float depth);
    // Digital full scale ↔ volts at the pedal's input (≈ a guitar's level at −12 dBFS);
    // the JFETs' grit depends on it.
    static constexpr float VOLTS_PER_UNIT = 1.0f;
};

} // namespace namx
