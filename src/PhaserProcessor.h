#pragma once

#include <atomic>
#include <array>
#include "EffectBase.h"

/// 4-stage all-pass phaser. Stereo uses 180° LFO offset.
class PhaserProcessor : public EffectBase
{
public:
    bool supportsStereoOut() const override { return true; }
    void reset() override;

    void setRate(float hz)      { mRate.store(hz);      }
    void setDepth(float pct)    { mDepth.store(pct);    }
    void setFeedback(float pct) { mFeedback.store(pct); }
    void setMix(float pct)      { mMix.store(pct);      }

    float rate()     const { return mRate.load();     }
    float depth()    const { return mDepth.load();    }
    float feedback() const { return mFeedback.load(); }
    float mix()      const { return mMix.load();      }

protected:
    void onSampleRateChanged(double /*sr*/) override { reset(); }
    void processBlockMono(float* buf, int nframes) override;
    void processBlockStereoOut(float* L, float* R, int nframes) override;

private:
    std::atomic<float> mRate    { 1.0f};
    std::atomic<float> mDepth   {50.0f};
    std::atomic<float> mFeedback{50.0f};
    std::atomic<float> mMix     {50.0f};

    float mPhase{0.0f};

    static constexpr int kStages = 4;

    std::array<float, kStages> mX1L{}, mY1L{};
    std::array<float, kStages> mX1R{}, mY1R{};
    float mFbStateL{0.0f}, mFbStateR{0.0f};

    static constexpr float kMinFreq = 200.0f;
    static constexpr float kMaxFreq = 4000.0f;

    float processChannel(float in, float phase,
                         std::array<float, kStages>& x1,
                         std::array<float, kStages>& y1,
                         float& fbState,
                         float depth, float fbAmt, float mix) const;
};
