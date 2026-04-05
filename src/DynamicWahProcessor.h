#pragma once

#include <atomic>
#include "EffectBase.h"

/// Dynamic wah (auto-wah / envelope filter).
///
/// An envelope follower tracks the input level and sweeps the cutoff of a
/// resonant state-variable filter (bandpass output).  Loud playing pushes
/// the filter open toward the peak frequency; soft playing lets it fall
/// back to the base frequency, giving the characteristic "wacka-wacka" sound.
///
/// Base frequency: ~400 Hz (heel-down position)
/// Peak frequency: ~3000 Hz (toe-down position)
class DynamicWahProcessor : public EffectBase
{
public:
    void setSampleRate(double sr) override;
    void reset() override;
    void processBlock(float* buf, int nframes) override;

    void setSensitivity(float pct) { mSensitivity.store(pct); }  // 0–100
    void setResonance(float pct)   { mResonance.store(pct);   }  // 0–100
    void setMix(float pct)         { mMix.store(pct);         }  // 0–100

    float sensitivity() const { return mSensitivity.load(); }
    float resonance()   const { return mResonance.load();   }
    float mix()         const { return mMix.load();         }

private:
    static constexpr float kBaseFreq = 400.0f;
    static constexpr float kPeakFreq = 3000.0f;

    std::atomic<float> mSensitivity{70.0f};
    std::atomic<float> mResonance  {60.0f};
    std::atomic<float> mMix        {100.0f};

    double mSampleRate {44100.0};

    // Envelope follower
    float mEnv        {0.0f};
    float mEnvAttack  {0.0f};
    float mEnvRelease {0.0f};

    // State-variable filter (Chamberlin)
    float mSVF_LP{0.0f};
    float mSVF_BP{0.0f};
};
