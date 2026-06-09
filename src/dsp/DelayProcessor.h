#pragma once

#include <atomic>
#include <vector>
#include "EffectBase.h"

/// Stereo ping-pong delay.
/// Mono in + mono mode: simple single-buffer delay.
/// Mono in + stereo mode (supportsStereoOut): ping-pong cross-feedback.
/// Stereo in (any mode): each channel delayed independently (processBlockDual).
class DelayProcessor : public EffectBase
{
public:
    bool supportsStereoOut() const override { return true; }
    void reset() override;

    void setTime(float ms)      { mTimeMS.store(ms);    }
    void setFeedback(float pct) { mFeedback.store(pct); }
    void setMix(float pct)      { mMix.store(pct);      }

    float time()     const { return mTimeMS.load();    }
    float feedback() const { return mFeedback.load();  }
    float mix()      const { return mMix.load();       }

protected:
    void onSampleRateChanged(double sr) override;
    void processBlockMono(float* buf, int nframes) override;
    void processBlockDual(float* L, float* R, int nframes) override;
    void processBlockStereoOut(float* L, float* R, int nframes) override;

private:
    std::atomic<float> mTimeMS  {250.0f};
    std::atomic<float> mFeedback{ 40.0f};
    std::atomic<float> mMix     { 30.0f};

    std::vector<float> mBufL, mBufR;
    int mWriteL{0}, mWriteR{0};

    static constexpr float kMaxDelayMs = 2000.0f;
};
