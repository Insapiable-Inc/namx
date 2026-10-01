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

    /// Deepest attenuation (dB, ≥ 0, capped at kMaxGrDb) since the last
    /// take — GUI meter.
    float takeGainReductionDb() { return mGrPeakDB.exchange(0.0f); }
    static constexpr float kMaxGrDb = 60.0f;

protected:
    void onSampleRateChanged(double /*sr*/) override { reset(); }
    void processBlockMono(float* buf, int nframes) override;
    void processBlockDual(float* L, float* R, int nframes) override;

private:
    void processChannel(float* buf, int nframes, int ch);

    // Defaults must match the knob defaults in Constants.h — the UI does not
    // push initial knob values to the engine at startup.
    std::atomic<float> mThresholdDB{-70.0f};
    std::atomic<float> mAttackMS   {  1.0f};
    std::atomic<float> mReleaseMS  {100.0f};

    // Per-channel state: [0] = mono/L, [1] = R (stereo input).
    float mEnvelope[2]{0.0f, 0.0f};
    float mGain[2]{0.0f, 0.0f};
    std::atomic<float> mGrPeakDB{0.0f};
};
