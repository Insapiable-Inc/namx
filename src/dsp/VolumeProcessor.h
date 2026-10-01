#pragma once

#include <atomic>

#include "Common.h"
#include "EffectBase.h"

namespace npl
{

/// Volume pedal: Position 0–100 % (heel = silence, toe = unity) on a curve
/// bent through the Mid level at the half-way point — set Mid low for swells
/// that stay quiet until late in the travel. Gain is smoothed (~10 ms), so an
/// expression pedal over MIDI never zips.
class VolumeProcessor : public EffectBase
{
public:
    void reset() override {
        gain = target_gain();
    }

    void set_position_pct(float pct) {
        position_pct.store(pct);
    }
    void set_mid(float db) {
        mid_db.store(db);
    }

    bool process(float* l, float* r, size_t n, bool input_is_stereo) override;

private:
    float target_gain() const;

    atomic<float> position_pct{100.0f};
    atomic<float> mid_db{-12.0f};
    float gain = 1.0f;
};

} // namespace npl
