#pragma once

#include <atomic>
#include "EffectBase.h"

/// Tremolo: amplitude modulation by a sine LFO.
/// Stereo: L/R LFOs are 90° apart for an autopanning effect.
class TremoloProcessor : public EffectBase
{
public:
    void setSampleRate(double sr) override { mSampleRate = sr; reset(); }
    void reset() override { mPhase = 0.0f; }

    void processBlock(float* buf, int nframes) override;
    void processBlockStereo(float* L, float* R, int nframes) override;

    void setRate(float hz)    { mRate.store(hz);    }
    void setDepth(float pct)  { mDepth.store(pct);  }  // 0–100

    float rate()  const { return mRate.load();  }
    float depth() const { return mDepth.load(); }

private:
    std::atomic<float> mRate { 5.0f};
    std::atomic<float> mDepth{50.0f};

    double mSampleRate{44100.0};
    float  mPhase{0.0f};
};
