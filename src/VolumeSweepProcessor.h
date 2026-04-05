#pragma once

#include <atomic>
#include "EffectBase.h"

/// Auto-swell (volume sweep).
///
/// Detects when a new note starts (signal rises above threshold) and
/// smoothly fades the volume in over the configured attack time, removing
/// the pick transient and creating a bowed/swelled character.  Volume resets
/// to zero when the signal falls below threshold, so each new pick triggers
/// a fresh swell.
class VolumeSweepProcessor : public EffectBase
{
public:
    void setSampleRate(double sr) override;
    void reset() override;
    void processBlock(float* buf, int nframes) override;

    void setAttack(float ms)      { mAttack.store(ms);      }  // 10–2000 ms
    void setSensitivity(float pct){ mSensitivity.store(pct);}  // 0–100
    void setMix(float pct)        { mMix.store(pct);        }  // 0–100

    float attack()      const { return mAttack.load();      }
    float sensitivity() const { return mSensitivity.load(); }
    float mix()         const { return mMix.load();         }

private:
    std::atomic<float> mAttack      {300.0f};
    std::atomic<float> mSensitivity { 50.0f};
    std::atomic<float> mMix         {100.0f};

    double mSampleRate {44100.0};
    float  mEnv        {0.0f};
    float  mEnvRelease {0.0f};
    float  mGain       {0.0f};
};
