#pragma once

#include <algorithm>
#include <atomic>

#include "Common.h"
#include "Cs3Compressor.h"
#include "EffectBase.h"

namespace namx
{

// Feedforward peak compressor.
//
// Type picks the engine: Compressor (the above, the default) or CS-3, a model of a Boss
// CS-3 Compression Sustainer (Cs3Compressor.h). In CS-3 Threshold is the pedal's Sustain
// (as the input level that reaches its fixed ceiling; clamped to the pedal's span), Release
// its Attack knob (as the gain's recovery time, 44–458 ms; the clamp-down follows), Level
// the output on top of the pedal at full Level, Tone (detail) its Tone; Ratio and Attack
// do nothing.
class CompressorProcessor : public EffectBase
{
public:
    // Type values (preset key compressor/type): append only.
    enum Type : int
    {
        COMPRESSOR = 0,
        CS3 = 1,
    };
    static constexpr int TYPE_COUNT = 2;

    void set_type(int new_type) {
        type.store(std::clamp(new_type, 0, TYPE_COUNT - 1));
    }
    int get_type() const {
        return type.load();
    }
    // CS-3 Tone: 0–100 % = 7:00–5:00 (50 = 12:00).
    void set_cs3_tone_pct(float pct) {
        cs3_tone_pct.store(pct);
    }

    void reset() override;
    void sync_right_channel() override;

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

    // Deepest gain reduction (dB, ≥ 0) since the last take — GUI meter.
    float take_gain_reduction_db() {
        return gr_peak_db.exchange(0.0f);
    }

protected:
    void on_sample_rate_changed(double /*sr*/) override {
        cs3_tone_fraction = -1.0f; // redesign at the new rate
        reset();
    }
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;

private:
    void process_channel(float* buf, size_t nframes, int ch);

    atomic<float> threshold_db{-20.0f};
    atomic<float> ratio{4.0f};
    atomic<float> attack_ms{10.0f};
    atomic<float> release_ms{100.0f};
    atomic<float> makeup_db{0.0f};

    // Per-channel state: [0] = mono/L, [1] = R (stereo input).
    float env_db_per_channel[2]{-120.0f, -120.0f};
    float gain_db_per_channel[2]{0.0f, 0.0f};
    atomic<float> gr_peak_db{0.0f};

    // CS-3 type.
    atomic<int> type{COMPRESSOR};
    atomic<float> cs3_tone_pct{50.0f};
    int last_type = COMPRESSOR; // audio thread
    float cs3_env[2]{0.0f, 0.0f}; // the peak detector, linear
    BiquadChain<cs3::TONE_SECTIONS> cs3_tone;
    BiquadChainState<cs3::TONE_SECTIONS> cs3_tone_state[2];
    float cs3_tone_fraction = -1.0f; // what cs3_tone is designed for
    void cs3_channel(float* buf, size_t nframes, int ch);
    void type_edge(int current_type);
};

} // namespace namx
