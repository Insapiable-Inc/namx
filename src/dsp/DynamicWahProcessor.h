#pragma once

#include <algorithm>
#include <atomic>

#include "Aw3Wah.h"
#include "Common.h"
#include "EffectBase.h"

namespace namx
{

// Wah: a model of a Boss AW-3 Dynamic Wah in its Up, Down or Sharp mode (Aw3Wah.h).
// Sensitivity is the pedal's Sens, Position its Manual (both 0–100 % = 7:00–5:00), Decay its
// Decay; Mix blends the dry signal back in.
class DynamicWahProcessor : public EffectBase
{
public:
    // Type values (preset key dynamicWah/type): append only. 0 was NAM-X's own envelope wah
    // (with its pedal mode), removed 2026-11-03 for the AW-3: a preset with it loads as Up.
    enum Type : int
    {
        AW3_UP = 1,
        AW3_DOWN = 2,
        AW3_SHARP = 3,
    };
    static constexpr int FIRST_TYPE = AW3_UP;
    static constexpr int LAST_TYPE = AW3_SHARP;

    void set_type(int new_type) {
        type.store(std::clamp(new_type, FIRST_TYPE, LAST_TYPE));
    }
    int get_type() const {
        return type.load();
    }
    // Decay: 0–100 % = 7:00–5:00 (50 = 12:00).
    void set_decay_pct(float pct) {
        decay_pct.store(pct);
    }

    void reset() override;
    void sync_right_channel() override;

    void set_sensitivity_pct(float pct) {
        sensitivity_pct.store(pct);
    }
    void set_mix_pct(float pct) {
        mix_pct.store(pct);
    }
    // The pedal's Manual: where the filter rests.
    void set_position_pct(float pct) {
        position_pct.store(pct);
    }

    float get_sensitivity_pct() const {
        return sensitivity_pct.load();
    }
    float get_mix_pct() const {
        return mix_pct.load();
    }

protected:
    void on_sample_rate_changed(double sr) override;
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;

private:
    atomic<float> sensitivity_pct{70.0f};
    atomic<float> mix_pct{100.0f};
    atomic<float> position_pct{50.0f};
    atomic<float> decay_pct{50.0f};
    atomic<int> type{AW3_UP};

    int last_type = AW3_UP; // audio thread
    aw3::Engine engine;
    // Per-channel state: [0] = mono/L, [1] = R (stereo input).
    aw3::Channel channels[2];
    void render(float* l, float* r, size_t nframes);
    void type_edge(int current_type);
};

} // namespace namx
