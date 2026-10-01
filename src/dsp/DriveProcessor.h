#pragma once

#include <atomic>
#include "EffectBase.h"

/// Overdrive / boost in front of the amp, Tube-Screamer style: only the
/// high-passed part of the signal (above ~720 Hz) goes through the gain +
/// soft-clip stage and is added back to the clean signal — the mid hump and
/// tight lows every high-gain rig wants — then a tone low-pass and level.
class DriveProcessor : public EffectBase
{
public:
    void reset() override;

    void setDrive(float pct)  { mDrive.store(pct); }  // 0–100
    void setTone(float pct)   { mTone.store(pct); }   // 0–100: dark → bright
    void setLevel(float dB)   { mLevelDb.store(dB); }

protected:
    void onSampleRateChanged(double) override { reset(); }
    void processBlockMono(float* buf, int n) override { processChannel(buf, n, 0); }
    void processBlockDual(float* L, float* R, int n) override
    {
        processChannel(L, n, 0);
        processChannel(R, n, 1);
    }

private:
    void processChannel(float* buf, int n, int ch);

    std::atomic<float> mDrive{30.0f};
    std::atomic<float> mTone{50.0f};
    std::atomic<float> mLevelDb{0.0f};

    // Per-channel state: input high-pass, tone low-pass.
    float mHpX1[2]{}, mHpY1[2]{}, mLp[2]{};
};
