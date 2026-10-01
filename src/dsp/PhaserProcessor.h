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
    /// 0 = classic phaser, 1 = Vibe.
    void setVibe(bool on)      { mVibe.store(on); }
    void setMix(float pct)      { mMix.store(pct);      }

    float rate()     const { return mRate.load();     }
    float depth()    const { return mDepth.load();    }
    float feedback() const { return mFeedback.load(); }
    float mix()      const { return mMix.load();      }

protected:
    void onSampleRateChanged(double /*sr*/) override { reset(); }
    void processBlockMono(float* buf, int nframes) override;
    void processBlockDual(float* L, float* R, int nframes) override;
    void processBlockStereoOut(float* L, float* R, int nframes) override;

private:
    std::atomic<float> mRate    { 1.0f};
    std::atomic<float> mDepth   {50.0f};
    std::atomic<float> mFeedback{50.0f};
    std::atomic<float> mMix     {50.0f};

    float mPhase{0.0f};

    static constexpr int kStages = 4;
    std::atomic<bool> mVibe{false};

    std::array<float, kStages> mX1L{}, mY1L{};
    std::array<float, kStages> mX1R{}, mY1R{};
    float mFbStateL{0.0f}, mFbStateR{0.0f};

    static constexpr float kMinFreq = 200.0f;
    static constexpr float kMaxFreq = 4000.0f;

    // All-pass coefficient for an LFO phase (sin + pow + tan). Evaluated at
    // control rate — every kCtrlInterval samples — not per sample.
    float coeffAt(float phase, float depth) const;
    // Vibe: four stages at staggered fixed ratios (the Uni-Vibe's mismatched
    // photocells), lamp-shaped LFO, no feedback.
    void  vibeCoeffs(float phase, float depth, std::array<float, kStages>& d) const;
    static constexpr int kCtrlInterval = 16; // 0.33 ms @ 48 kHz; LFO ≤ 5 Hz

    float processVibe(float in, const std::array<float, kStages>& d,
                      std::array<float, kStages>& x1, std::array<float, kStages>& y1, float mix) const;
    float processChannel(float in, float d,
                         std::array<float, kStages>& x1,
                         std::array<float, kStages>& y1,
                         float& fbState, float fbAmt, float mix) const;
};
