#pragma once

#include <atomic>
#include "EffectBase.h"

/// Feedforward peak compressor with adjustable threshold, ratio, attack/release and makeup gain.
class CompressorProcessor : public EffectBase
{
public:
    void setSampleRate(double sr) override;
    void reset() override;
    void processBlock(float* buf, int nframes) override;

    void setThreshold(float dB)  { mThresholdDB.store(dB);  }
    void setRatio(float ratio)   { mRatio.store(ratio);      }
    void setAttack(float ms)     { mAttackMS.store(ms);      }
    void setRelease(float ms)    { mReleaseMS.store(ms);     }
    void setMakeup(float dB)     { mMakeupDB.store(dB);      }

    float threshold() const { return mThresholdDB.load(); }
    float ratio()     const { return mRatio.load();       }
    float attack()    const { return mAttackMS.load();    }
    float release()   const { return mReleaseMS.load();   }
    float makeup()    const { return mMakeupDB.load();    }

private:
    std::atomic<float> mThresholdDB{-20.0f};
    std::atomic<float> mRatio       { 4.0f};
    std::atomic<float> mAttackMS    {10.0f};
    std::atomic<float> mReleaseMS   {100.0f};
    std::atomic<float> mMakeupDB    { 0.0f};

    double mSampleRate{44100.0};
    float  mEnvDB{-120.0f}; // smoothed level in dB
    float  mGainDB{0.0f};   // smoothed gain reduction in dB
};
