#pragma once

#include <atomic>
#include <array>
#include "EffectBase.h"

/// 4-stage all-pass phaser with LFO modulation.
/// Stereo: L/R run with 180° LFO phase offset.
class PhaserProcessor : public EffectBase
{
public:
    void setSampleRate(double sr) override;
    void reset() override;

    void processBlock(float* buf, int nframes) override;
    void processBlockStereo(float* L, float* R, int nframes) override;

    void setRate(float hz)        { mRate.store(hz);      }
    void setDepth(float pct)      { mDepth.store(pct);    }  // 0–100
    void setFeedback(float pct)   { mFeedback.store(pct); }  // 0–99
    void setMix(float pct)        { mMix.store(pct);      }  // 0–100

    float rate()     const { return mRate.load();     }
    float depth()    const { return mDepth.load();    }
    float feedback() const { return mFeedback.load(); }
    float mix()      const { return mMix.load();      }

private:
    std::atomic<float> mRate    { 1.0f};
    std::atomic<float> mDepth   {50.0f};
    std::atomic<float> mFeedback{50.0f};
    std::atomic<float> mMix     {50.0f};

    double mSampleRate{44100.0};
    float  mPhase{0.0f};

    static constexpr int kStages = 4;

    // First-order all-pass state: x1, y1 per stage, for L and R
    std::array<float, kStages> mX1L{}, mY1L{};
    std::array<float, kStages> mX1R{}, mY1R{};
    float mFbStateL{0.0f}, mFbStateR{0.0f};

    // LFO sweeps center frequency between these bounds
    static constexpr float kMinFreq = 200.0f;
    static constexpr float kMaxFreq = 4000.0f;

    float processChannel(float in, float phase,
                         std::array<float, kStages>& x1,
                         std::array<float, kStages>& y1,
                         float& fbState,
                         float depth, float fbAmt, float mix) const;
};
