#pragma once

#include <atomic>

#include "EffectBase.h"

/// Feedforward peak compressor.
class CompressorProcessor : public EffectBase
{
public:
    void reset() override;

    void setThreshold(float dB) {
        mThresholdDB.store(dB);
    }
    void setRatio(float ratio) {
        mRatio.store(ratio);
    }
    void setAttack(float ms) {
        mAttackMS.store(ms);
    }
    void setRelease(float ms) {
        mReleaseMS.store(ms);
    }
    void setMakeup(float dB) {
        mMakeupDB.store(dB);
    }

    float threshold() const {
        return mThresholdDB.load();
    }
    float ratio() const {
        return mRatio.load();
    }
    float attack() const {
        return mAttackMS.load();
    }
    float release() const {
        return mReleaseMS.load();
    }
    float makeup() const {
        return mMakeupDB.load();
    }

    /// Deepest gain reduction (dB, ≥ 0) since the last take — GUI meter.
    float takeGainReductionDb() {
        return mGrPeakDB.exchange(0.0f);
    }

protected:
    void onSampleRateChanged(double /*sr*/) override {
        reset();
    }
    void processBlockMono(float* buf, size_t nframes) override;
    void processBlockDual(float* L, float* R, size_t nframes) override;

private:
    void processChannel(float* buf, size_t nframes, int ch);

    std::atomic<float> mThresholdDB{-20.0f};
    std::atomic<float> mRatio{4.0f};
    std::atomic<float> mAttackMS{10.0f};
    std::atomic<float> mReleaseMS{100.0f};
    std::atomic<float> mMakeupDB{0.0f};

    // Per-channel state: [0] = mono/L, [1] = R (stereo input).
    float mEnvDB[2]{-120.0f, -120.0f};
    float mGainDB[2]{0.0f, 0.0f};
    std::atomic<float> mGrPeakDB{0.0f};
};
