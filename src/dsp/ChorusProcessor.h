#pragma once

#include <atomic>

#include "DelayLine.h"
#include "EffectBase.h"

/// Chorus: LFO-modulated delay. Modes: Normal (one voice), Tri (three voices
/// 120° apart — lusher), Vintage (triangle LFO, darker BBD-style wet),
/// Spatial (stereo: 90° LFOs, right wet phase-inverted for extra width).
/// Stereo mode uses a 180° offset (90° in Spatial).
class ChorusProcessor : public EffectBase
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
    void setMix(float pct) {
        mMix.store(pct);
    }
    enum Mode
    {
        Normal = 0,
        Tri,
        Vintage,
        Spatial
    };
    void setMode(int m) {
        mMode.store(m);
    }

    float rate() const {
        return mRate.load();
    }
    float depth() const {
        return mDepth.load();
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
    std::atomic<float> mRate{1.0f};
    std::atomic<float> mDepth{50.0f};
    std::atomic<float> mMix{50.0f};

    float mPhase{0.0f};
    std::atomic<int> mMode{Normal};
    float mVintLp[2]{}; // Vintage wet low-pass per channel

    // Wet sample for one channel at an LFO phase (all modes).
    float wet(const DelayLine& line, float phase, float depth, float fs, int mode);

    DelayLine mLineL, mLineR;

    static constexpr float kBaseMs = 20.0f;
    static constexpr float kMaxDepthMs = 5.0f;
};
