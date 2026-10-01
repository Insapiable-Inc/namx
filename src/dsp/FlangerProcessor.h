#pragma once

#include <atomic>

#include "DelayLine.h"
#include "EffectBase.h"

/// Flanger: short modulated delay with feedback. Stereo uses 180° LFO offset.
class FlangerProcessor : public EffectBase
{
public:
    bool supportsStereoOut() const override {
        return true;
    }
    void reset() override;

    void setRate(float hz) {
        mRate.store(hz);
    }
    void setDepth(float pct) {
        mDepth.store(pct);
    }
    void setFeedback(float pct) {
        mFeedback.store(pct);
    }
    void setMix(float pct) {
        mMix.store(pct);
    }
    /// 0 = short, metallic sweep (≈0–3 ms) … 100 = long jet sweep (0–14 ms).
    void setRange(float pct) {
        mRange.store(pct);
    }

    float rate() const {
        return mRate.load();
    }
    float depth() const {
        return mDepth.load();
    }
    float feedback() const {
        return mFeedback.load();
    }
    float mix() const {
        return mMix.load();
    }

protected:
    void onSampleRateChanged(double sr) override;
    void processBlockMono(float* buf, int nframes) override;
    void processBlockDual(float* L, float* R, int nframes) override;
    void processBlockStereoOut(float* L, float* R, int nframes) override;

private:
    std::atomic<float> mRate{0.3f}; // matches kFlangerRateDefault
    std::atomic<float> mDepth{50.0f};
    std::atomic<float> mFeedback{0.0f};
    std::atomic<float> mMix{50.0f};
    std::atomic<float> mRange{100.0f};

    float mPhase{0.0f};

    DelayLine mLineL, mLineR;
    float mFbL{0.0f}, mFbR{0.0f};

    static constexpr float kBaseMs = 7.0f;
    static constexpr float kDepthMs = 7.0f;

    void processSample(float in, float phase, DelayLine& line, float& fb, float depth, float fbAmt,
                       float mix, float& out) const;
};
