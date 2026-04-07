#pragma once

#include <atomic>
#include "EffectBase.h"

/// Simple envelope-following noise gate.
class NoiseGateProcessor : public EffectBase
{
public:
    void reset() override;

    void setThreshold(float dB) { mThresholdDB.store(dB); }
    void setAttack(float ms)    { mAttackMS.store(ms);    }
    void setRelease(float ms)   { mReleaseMS.store(ms);   }

    float threshold() const { return mThresholdDB.load(); }
    float attack()    const { return mAttackMS.load();    }
    float release()   const { return mReleaseMS.load();   }

protected:
    void onSampleRateChanged(double /*sr*/) override { reset(); }
    void processBlockMono(float* buf, int nframes) override;

private:
    std::atomic<float> mThresholdDB{-40.0f};
    std::atomic<float> mAttackMS   { 10.0f};
    std::atomic<float> mReleaseMS  {100.0f};

    float mEnvelope{0.0f};
    float mGain{0.0f};
};
