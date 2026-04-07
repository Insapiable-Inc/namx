#pragma once

#include <atomic>
#include <vector>
#include "EffectBase.h"

/// Chorus: LFO-modulated delay. Stereo mode uses 180° phase offset for width.
class ChorusProcessor : public EffectBase
{
public:
    bool supportsStereoOut() const override { return true; }
    void reset() override;

    void setRate(float hz)   { mRate.store(hz);   }
    void setDepth(float pct) { mDepth.store(pct); }
    void setMix(float pct)   { mMix.store(pct);   }

    float rate()  const { return mRate.load();  }
    float depth() const { return mDepth.load(); }
    float mix()   const { return mMix.load();   }

protected:
    void onSampleRateChanged(double sr) override;
    void processBlockMono(float* buf, int nframes) override;
    void processBlockStereoOut(float* L, float* R, int nframes) override;

private:
    std::atomic<float> mRate { 1.0f};
    std::atomic<float> mDepth{50.0f};
    std::atomic<float> mMix  {50.0f};

    float mPhase{0.0f};

    std::vector<float> mBufL, mBufR;
    int mWriteL{0}, mWriteR{0};

    static constexpr float kBaseMs    = 20.0f;
    static constexpr float kMaxDepthMs =  5.0f;

    float readInterp(const std::vector<float>& buf, int writePos, float delaySamples) const;
};
