#pragma once

#include <atomic>
#include <vector>
#include "EffectBase.h"

/// Stereo ping-pong delay.
/// In mono mode: simple mono delay on a single buffer.
/// In stereo mode: L signal echoes into R, R into L (ping-pong).
class DelayProcessor : public EffectBase
{
public:
    void setSampleRate(double sr) override;
    void reset() override;

    void processBlock(float* buf, int nframes) override;
    void processBlockStereo(float* L, float* R, int nframes) override;
    void processBlockStereoIndependent(float* L, float* R, int nframes);

    void setTime(float ms)        { mTimeMS.store(ms);     }  // 0–2000
    void setFeedback(float pct)   { mFeedback.store(pct);  }  // 0–99
    void setMix(float pct)        { mMix.store(pct);       }  // 0–100

    float time()     const { return mTimeMS.load();    }
    float feedback() const { return mFeedback.load();  }
    float mix()      const { return mMix.load();       }

private:
    std::atomic<float> mTimeMS  {250.0f};
    std::atomic<float> mFeedback{ 40.0f};
    std::atomic<float> mMix     { 30.0f};

    double mSampleRate{44100.0};

    std::vector<float> mBufL, mBufR;
    int mWriteL{0}, mWriteR{0};

    static constexpr float kMaxDelayMs = 2000.0f;
};
