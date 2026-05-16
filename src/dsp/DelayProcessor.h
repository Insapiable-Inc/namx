#pragma once

#include <atomic>
#include <vector>
#include "EffectBase.h"

/// Stereo ping-pong delay.
/// Mono mode: simple single-buffer delay.
/// Stereo mode (supportsStereoOut): ping-pong cross-feedback.
/// Stereo in + mono mode: each channel delayed independently (base class dispatch).
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

    /// Always routes through processBlockStereoOut when stereo mode is on so
    /// both delay buffers are used correctly. The base-class fallback for
    /// inputIsStereo=true calls processBlockMono twice, which double-advances
    /// mWriteL and reads from the wrong positions.
    bool process(float* L, float* R, int n, bool inputIsStereo) override;

protected:
    void onSampleRateChanged(double sr) override;
    void processBlockMono(float* buf, int nframes) override;
    void processBlockStereoOut(float* L, float* R, int nframes) override;

private:
    std::atomic<float> mTimeMS  {250.0f};
    std::atomic<float> mFeedback{ 40.0f};
    std::atomic<float> mMix     { 30.0f};

    std::vector<float> mBufL, mBufR;
    int mWriteL{0}, mWriteR{0};

    static constexpr float kMaxDelayMs = 2000.0f;
};
