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
        mGain = targetGain();
    }

    void setPosition(float pct) {
        mPosition.store(pct);
    }
    void setMid(float dB) {
        mMidDb.store(dB);
    }

    bool process(float* L, float* R, size_t n, bool inputIsStereo) override;

private:
    float targetGain() const;

    std::atomic<float> mPosition{100.0f};
    std::atomic<float> mMidDb{-12.0f};
    float mGain = 1.0f;
};
