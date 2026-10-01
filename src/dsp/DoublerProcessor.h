#pragma once

#include <atomic>
#include <cstdint>

#include "DelayLine.h"
#include "EffectBase.h"

/// Double-tracking: from a mono signal, the right channel becomes a second
/// "take" — a copy delayed 3–20 ms whose delay drifts randomly (timing and
/// a little pitch), the left stays dry. A stereo input passes untouched:
/// there is nothing mono left to double.
class DoublerProcessor : public EffectBase
{
public:
    bool supportsStereoOut() const override {
        return true;
    }
    void reset() override;

    void setSpread(float ms) {
        mSpreadMs.store(ms);
    }
    void setMix(float pct) {
        mMix.store(pct);
    }

    bool process(float* L, float* R, size_t n, bool inputIsStereo) override;

protected:
    void onSampleRateChanged(double sr) override;

private:
    std::atomic<float> mSpreadMs{10.0f};
    std::atomic<float> mMix{100.0f};

    DelayLine mLine;
    float mDrift = 0.0f, mDriftTarget = 0.0f; // −1 … 1
    int mUntilNewTarget = 0;
    uint32_t mSeed = 0x9e3779b9u;
};
