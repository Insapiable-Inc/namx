#pragma once

#include <atomic>

#include "EffectBase.h"

/// Feedforward peak compressor.
class CompressorProcessor : public EffectBase
{
public:
    void reset() override;

    void set_threshold(float db) {
        threshold_db.store(db);
    }
    void set_ratio(float new_ratio) {
        ratio.store(new_ratio);
    }
    void set_attack(float ms) {
        attack_ms.store(ms);
    }
    void set_release(float ms) {
        release_ms.store(ms);
    }
    void set_makeup(float db) {
        makeup_db.store(db);
    }

    float threshold() const {
        return threshold_db.load();
    }
    float get_ratio() const {
        return ratio.load();
    }
    float attack() const {
        return attack_ms.load();
    }
    float release() const {
        return release_ms.load();
    }
    float makeup() const {
        return makeup_db.load();
    }

    /// Deepest gain reduction (dB, ≥ 0) since the last take — GUI meter.
    float take_gain_reduction_db() {
        return gr_peak_db.exchange(0.0f);
    }

protected:
    void on_sample_rate_changed(double /*sr*/) override {
        reset();
    }
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;

private:
    void process_channel(float* buf, size_t nframes, int ch);

    std::atomic<float> threshold_db{-20.0f};
    std::atomic<float> ratio{4.0f};
    std::atomic<float> attack_ms{10.0f};
    std::atomic<float> release_ms{100.0f};
    std::atomic<float> makeup_db{0.0f};

    // Per-channel state: [0] = mono/L, [1] = R (stereo input).
    float env_db_per_channel[2]{-120.0f, -120.0f};
    float gain_db_per_channel[2]{0.0f, 0.0f};
    std::atomic<float> gr_peak_db{0.0f};
};
