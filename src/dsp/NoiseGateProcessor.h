#pragma once

#include <algorithm>
#include <atomic>

#include "Common.h"
#include "EffectBase.h"
#include "Ns2Gate.h"

namespace namx
{

// Simple envelope-following noise gate.
//
// Type picks the engine: Gate (the above, the default) or NS-2, a model of a Boss NS-2
// Noise Suppressor in its Reduction mode (Ns2Gate.h). In NS-2 Threshold is where the gate
// opens (clamped to the pedal's span), Release its Decay (the fast level's fall, 25–460 ms;
// the rest of the closing follows); Attack does nothing — it opens at once.
class NoiseGateProcessor : public EffectBase
{
public:
    // Type values (preset key noiseGate/type): append only.
    enum Type : int
    {
        GATE = 0,
        NS2 = 1,
    };
    static constexpr int TYPE_COUNT = 2;

    void set_type(int new_type) {
        type.store(std::clamp(new_type, 0, TYPE_COUNT - 1));
    }
    int get_type() const {
        return type.load();
    }

    void reset() override;
    void sync_right_channel() override;

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
    atomic<float> attack_ms{1.0f};
    atomic<float> release_ms{100.0f};

    // Per-channel state: [0] = mono/L, [1] = R (stereo input).
    float envelope[2]{0.0f, 0.0f};
    float gain_per_channel[2]{0.0f, 0.0f};
    atomic<float> gr_peak_db{0.0f};

    // NS-2 type.
    struct Ns2State
    {
        float peak = 0.0f; // the input's peak follower, linear
        float fast_db = ns2::FLOOR_DB;
        float slow_db = ns2::FLOOR_DB;
        float switch_db = -ns2::JUMP_DB; // G: 0 open, sinking toward −JUMP_DB closed
        bool is_open = false;
    };
    atomic<int> type{GATE};
    int last_type = GATE; // audio thread
    Ns2State ns2_state[2];
    BiquadChain<ns2::TONE_SECTIONS> ns2_tone;
    double ns2_tone_rate = 0.0; // what ns2_tone is designed for (audio thread)
    BiquadChainState<ns2::TONE_SECTIONS> ns2_tone_state[2];
    void ns2_channel(float* buf, size_t nframes, int ch);
    void type_edge(int current_type);
};

} // namespace namx
