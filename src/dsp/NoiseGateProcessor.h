#pragma once

#include <atomic>

#include "Common.h"
#include "EffectBase.h"
#include "Ns2Gate.h"

namespace namx
{

// Noise gate: a model of a Boss NS-2 Noise Suppressor in its Reduction mode (Ns2Gate.h).
// Threshold and Release are the pedal's Threshold and Decay over their spans. It opens at
// once: no attack.
class NoiseGateProcessor : public EffectBase
{
public:
    void reset() override;
    void sync_right_channel() override;

    void set_threshold(float db) {
        threshold_db.store(db);
    }
    void set_release(float ms) {
        release_ms.store(ms);
    }

    float threshold() const {
        return threshold_db.load();
    }
    float release() const {
        return release_ms.load();
    }

    // Deepest attenuation (dB, ≥ 0, capped at MAX_GR_DB) since the last
    // take — GUI meter.
    float take_gain_reduction_db() {
        return gr_peak_db.exchange(0.0f);
    }
    static constexpr float MAX_GR_DB = 60.0f;

protected:
    void on_sample_rate_changed(double /*sr*/) override {
        reset();
    }
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;

private:
    void process_channel(float* buf, size_t nframes, int ch);

    // Defaults must match the knob defaults in Constants.h — the UI does not
    // push initial knob values to the engine at startup.
    atomic<float> threshold_db{-70.0f};
    atomic<float> release_ms{100.0f};
    atomic<float> gr_peak_db{0.0f};

    // Per channel: [0] = mono/L, [1] = R (stereo input).
    ns2::Control control[2];
    BiquadChain<ns2::TONE_SECTIONS> tone;
    double prepared_rate = 0.0; // what `tone` and `control` are prepared for (audio thread)
    BiquadChainState<ns2::TONE_SECTIONS> tone_state[2];
};

} // namespace namx
