#pragma once

#include <atomic>
#include <vector>
#include "EffectBase.h"

/// Flanger effect: short modulated delay with feedback.
/// Stereo: L/R LFOs are 180° apart for a wide, sweeping sound.
class FlangerProcessor : public EffectBase
{
public:
    void setSampleRate(double sr) override;
    void reset() override;

    void processBlock(float* buf, int nframes) override;
    void processBlockStereo(float* L, float* R, int nframes) override;

    void setRate(float hz)         { mRate.store(hz);       }
    void setDepth(float pct)       { mDepth.store(pct);     }  // 0–100
    void setFeedback(float pct)    { mFeedback.store(pct);  }  // -99–99
    void setMix(float pct)         { mMix.store(pct);       }  // 0–100

    float rate()     const { return mRate.load();     }
    float depth()    const { return mDepth.load();    }
    float feedback() const { return mFeedback.load(); }
    float mix()      const { return mMix.load();      }

private:
    std::atomic<float> mRate    { 0.5f};
    std::atomic<float> mDepth   {50.0f};
    std::atomic<float> mFeedback{ 0.0f};
    std::atomic<float> mMix     {50.0f};

    double mSampleRate{44100.0};
    float  mPhase{0.0f};

    std::vector<float> mBufL, mBufR;
    int   mWriteL{0}, mWriteR{0};
    float mFbL{0.0f}, mFbR{0.0f}; // last output for feedback

    // Delay range: 0–15ms, base at 7ms
    static constexpr float kBaseMs  =  7.0f;
    static constexpr float kDepthMs =  7.0f;

    float readInterp(const std::vector<float>& buf, int writePos, float delaySamples) const;
    void  processSample(float in, float phase, std::vector<float>& buf, int& writePos, float& fb,
                        float depth, float fbAmt, float mix, float& out) const;
};
