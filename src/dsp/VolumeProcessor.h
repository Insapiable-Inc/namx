#pragma once

#include <atomic>

#include "EffectBase.h"

/// Volume pedal: Position 0–100 % (heel = silence, toe = unity) on a curve
/// bent through the Mid level at the half-way point — set Mid low for swells
/// that stay quiet until late in the travel. Gain is smoothed (~10 ms), so an
/// expression pedal over MIDI never zips.
class VolumeProcessor : public EffectBase
{
public:
    void reset() override {
        m_gain = target_gain();
    }

    void set_position_pct(float pct) {
        m_position_pct.store(pct);
    }
    void set_mid(float db) {
        m_mid_db.store(db);
    }

    bool process(float* l, float* r, size_t n, bool input_is_stereo) override;

private:
    float target_gain() const;

    std::atomic<float> m_position_pct{100.0f};
    std::atomic<float> m_mid_db{-12.0f};
    float m_gain = 1.0f;
};
