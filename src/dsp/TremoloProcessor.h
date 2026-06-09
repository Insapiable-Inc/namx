#pragma once

#include <atomic>
#include "EffectBase.h"

/// Tremolo: amplitude modulation. Stereo uses 90° LFO offset for auto-pan.
class TremoloProcessor : public EffectBase
{
public:
    bool supportsStereoOut() const override { return true; }
    void reset() override { mPhase = 0.0f; }

    void setRate(float hz)   { mRate.store(hz);   }
    void setDepth(float pct) { mDepth.store(pct); }

    float rate()  const { return mRate.load();  }
    float depth() const { return mDepth.load(); }

protected:
    void onSampleRateChanged(double /*sr*/) override { reset(); }
    void processBlockMono(float* buf, int nframes) override;
    void processBlockDual(float* L, float* R, int nframes) override;
    void processBlockStereoOut(float* L, float* R, int nframes) override;

private:
    std::atomic<float> mRate { 5.0f};
    std::atomic<float> mDepth{50.0f};

    float mPhase{0.0f};
};
