#pragma once

#include <atomic>

#include "EffectBase.h"

/// Simple envelope-following noise gate.
class NoiseGateProcessor : public EffectBase
{
public:
    void reset() override;

    void set_threshold(float db) {
        threshold_db.store(db);
    }
    void set_attack(float ms) {
        attack_ms.store(ms);
    }
    void set_release(float ms) {
        release_ms.store(ms);
    }

    float threshold() const {
        return threshold_db.load();
    }
    float attack() const {
        return attack_ms.load();
    }
    float release() const {
        return release_ms.load();
    }

    /// Deepest attenuation (dB, ≥ 0, capped at MAX_GR_DB) since the last
    /// take — GUI meter.
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
    std::atomic<float> threshold_db{-70.0f};
    std::atomic<float> attack_ms{1.0f};
    std::atomic<float> release_ms{100.0f};

    // Per-channel state: [0] = mono/L, [1] = R (stereo input).
    float envelope[2]{0.0f, 0.0f};
    float gain_per_channel[2]{0.0f, 0.0f};
    std::atomic<float> gr_peak_db{0.0f};
};
