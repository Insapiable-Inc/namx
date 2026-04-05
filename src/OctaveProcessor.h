#pragma once

#include <atomic>
#include "EffectBase.h"

/// Octave pedal (mono, pre-NAM).
///
/// Oct Up  — full-wave rectification doubles the fundamental, producing a
///           signal one octave above the input.  A DC-blocker removes the
///           low-frequency offset that rectification introduces.
///
/// Oct Down — zero-crossing flip-flop toggles a square wave on each
///            zero-crossing of the input, yielding a signal one octave below.
///            An envelope follower (fast attack, slow release) gives it
///            dynamics so it tracks the playing.
///
/// Both are blended additively with the dry signal (dry is always present,
/// identical to a real OC-2 / Boss-style octave pedal).
class OctaveProcessor : public EffectBase
{
public:
    void setSampleRate(double sr) override;
    void reset() override;
    void processBlock(float* buf, int nframes) override;

    void setUpLevel(float pct)   { mUpLevel.store(pct);   }  // 0–100
    void setDownLevel(float pct) { mDownLevel.store(pct); }  // 0–100

    float upLevel()   const { return mUpLevel.load();   }
    float downLevel() const { return mDownLevel.load(); }

private:
    std::atomic<float> mUpLevel  {50.0f};
    std::atomic<float> mDownLevel{50.0f};

    double mSampleRate{44100.0};

    // Oct-up DC blocker state
    float mPrevRect {0.0f};
    float mDCState  {0.0f};

    // Oct-down state
    float mFlipFlop  { 1.0f};
    float mPrevSample{ 0.0f};
    float mEnvelope  { 0.0f};
    float mEnvRelease{ 0.0f};  // precomputed per-sample decay factor
};
