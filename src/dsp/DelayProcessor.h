#pragma once

#include <atomic>

#include "DelayLine.h"
#include "EffectBase.h"
#include "PitchShifter.h"

/// Stereo delay.
/// Mono in + mono mode: one delay line.
/// Mono in + stereo mode: two lines — left at Time, right at Time × R Time %
///   — with Cross-feed blending the repeats across: 100 % = ping-pong
///   (left → right → left …), 0 % = two independent echoes.
/// Stereo in (any mode): each channel through its own line (L/R times).
/// Every mode: Low/High Cut and Saturation act inside the feedback loop, so
/// each repeat is darker / dirtier than the last (tape/analog character);
/// Modulation wobbles the delay time (tape flutter / chorus on the repeats).
class DelayProcessor : public EffectBase
{
public:
    bool supportsStereoOut() const override {
        return true;
    }
    void reset() override;

    void setTime(float ms) {
        mTimeMS.store(ms);
    }
    void setFeedback(float pct) {
        mFeedback.store(pct);
    }
    void setMix(float pct) {
        mMix.store(pct);
    }
    void setRightRatio(float pct) {
        mRightPct.store(pct);
    } // 25–200 % of Time
    void setCrossFeed(float pct) {
        mCrossPct.store(pct);
    }
    void setLowCut(float hz) {
        mLowCutHz.store(hz);
    }
    void setHighCut(float hz) {
        mHighCutHz.store(hz);
    }
    void setSaturation(float pct) {
        mSatPct.store(pct);
    }
    void setModulation(float pct) {
        mModPct.store(pct);
    }
    /// Octave-up repeats (0–100 %): each pass through the loop climbs.
    void setCrystals(float pct) {
        mCrystalsPct.store(pct);
    }

    float time() const {
        return mTimeMS.load();
    }
    float feedback() const {
        return mFeedback.load();
    }
    float mix() const {
        return mMix.load();
    }

protected:
    void onSampleRateChanged(double sr) override;
    void processBlockMono(float* buf, size_t nframes) override;
    void processBlockDual(float* L, float* R, size_t nframes) override;
    void processBlockStereoOut(float* L, float* R, size_t nframes) override;

private:
    struct Line
    {
        DelayLine samples;
        float hpX = 0.0f, hpY = 0.0f, lp = 0.0f;
        // Read before this sample's push(), so "d samples ago" is d - 1 behind the
        // newest sample in the line.
        float read(float delaySamples) const {
            return samples.interpolate(delaySamples - 1.0f);
        }
        void push(float x) {
            samples.push(x);
        }
    };

    // Per-block derived values.
    struct Params
    {
        float fb, mix, cross, hpA, lpK, satDrive /* 0–1 */, modDepth, phInc;
        float delL, delR; // samples
        float crystals; // 0–1
    };
    Params params() const;
    float tone(Line& l, float x, const Params& p) const; // feedback-loop cuts
    float loop(int line, float d, const Params& p); // what re-enters the line (crystals)
    static float saturate(float x, float amount);

    std::atomic<float> mTimeMS{250.0f};
    std::atomic<float> mFeedback{40.0f};
    std::atomic<float> mMix{30.0f};
    std::atomic<float> mRightPct{100.0f};
    std::atomic<float> mCrossPct{100.0f};
    std::atomic<float> mLowCutHz{20.0f};
    std::atomic<float> mHighCutHz{20000.0f};
    std::atomic<float> mSatPct{0.0f};
    std::atomic<float> mModPct{0.0f};
    std::atomic<float> mCrystalsPct{0.0f};
    PitchShifter mShift[2];

    Line mLine[2];
    float mModPhase = 0.0f;

    static constexpr float kMaxDelayMs = 2100.0f; // 1000 ms × 200 % + modulation
};
