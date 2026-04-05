#pragma once

#include <atomic>
#include "EffectBase.h"

/// Bit crusher: reduces the effective bit depth of the signal.
///
/// Quantises the signal to 2^(bits-1) steps per polarity, producing
/// the characteristic "crunchy / lo-fi" distortion.  Fractional bit
/// values give smooth transitions between integer depths.
class BitCrusherProcessor : public EffectBase
{
public:
    void setSampleRate(double sr) override { mSampleRate = sr; }
    void reset() override {}

    void processBlock(float* buf, int nframes) override;
    void processBlockStereo(float* L, float* R, int nframes) override;

    void setBits(float bits) { mBits.store(bits); }  // 1–16
    void setMix(float pct)   { mMix.store(pct);   }  // 0–100

    float bits() const { return mBits.load(); }
    float mix()  const { return mMix.load();  }

private:
    std::atomic<float> mBits{8.0f};
    std::atomic<float> mMix {100.0f};

    double mSampleRate{44100.0};
};
