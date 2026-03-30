#pragma once

#include <atomic>
#include <vector>
#include "EffectBase.h"

/// Chorus effect using two modulated delay lines.
/// Mono mode: single delay line, LFO in-phase.
/// Stereo mode: two delay lines, LFO 180° apart for a wide, lush sound.
class ChorusProcessor : public EffectBase
{
public:
    void setSampleRate(double sr) override;
    void reset() override;

    void processBlock(float* buf, int nframes) override;
    void processBlockStereo(float* L, float* R, int nframes) override;

    void setRate(float hz)    { mRate.store(hz);    }
    void setDepth(float pct)  { mDepth.store(pct);  }  // 0–100
    void setMix(float pct)    { mMix.store(pct);    }  // 0–100

    float rate()  const { return mRate.load();  }
    float depth() const { return mDepth.load(); }
    float mix()   const { return mMix.load();   }

private:
    std::atomic<float> mRate { 1.0f};
    std::atomic<float> mDepth{50.0f};
    std::atomic<float> mMix  {50.0f};

    double mSampleRate{44100.0};
    float  mPhase{0.0f};

    // Delay buffers
    std::vector<float> mBufL, mBufR;
    int mWriteL{0}, mWriteR{0};

    // Base delay ~20ms, depth up to 5ms
    static constexpr float kBaseMs = 20.0f;
    static constexpr float kMaxDepthMs = 5.0f;

    float readInterp(const std::vector<float>& buf, int writePos, float delaySamples) const;
};
