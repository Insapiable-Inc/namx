#pragma once

#include <atomic>
#include "EffectBase.h"

/// Dynamic wah (auto-wah): envelope follower sweeps a resonant SVF bandpass.
class DynamicWahProcessor : public EffectBase
{
public:
    void reset() override;

    void setSensitivity(float pct) { mSensitivity.store(pct); }
    void setResonance(float pct)   { mResonance.store(pct);   }
    void setMix(float pct)         { mMix.store(pct);         }

    float sensitivity() const { return mSensitivity.load(); }
    float resonance()   const { return mResonance.load();   }
    float mix()         const { return mMix.load();         }

protected:
    void onSampleRateChanged(double sr) override;
    void processBlockMono(float* buf, int nframes) override;

private:
    static constexpr float kBaseFreq = 400.0f;
    static constexpr float kPeakFreq = 3000.0f;

    std::atomic<float> mSensitivity{70.0f};
    std::atomic<float> mResonance  {60.0f};
    std::atomic<float> mMix        {100.0f};

    float mEnv       {0.0f};
    float mEnvAttack {0.0f};
    float mEnvRelease{0.0f};

    float mSVF_LP{0.0f};
    float mSVF_BP{0.0f};
};
