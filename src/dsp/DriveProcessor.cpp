#include "DriveProcessor.h"

#include <algorithm>
#include <cmath>

static constexpr float kPi = 3.14159265358979323846f;

void DriveProcessor::reset()
{
    for (int c = 0; c < 2; ++c) mHpX1[c] = mHpY1[c] = mLp[c] = 0.0f;
}

// Smooth symmetric clip (Padé tanh, exact at ±3) — diode-pair-like knee.
static inline float softClip(float x)
{
    x = std::clamp(x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

void DriveProcessor::processChannel(float* buf, int n, int ch)
{
    const float fs    = static_cast<float>(mSampleRate);
    const float drive = std::clamp(mDrive.load(), 0.0f, 100.0f) / 100.0f;
    // 0 → +6 dB, 100 % → +46 dB on the clipping path (the TS's 51k–551k range).
    const float gain  = std::pow(10.0f, (6.0f + 40.0f * drive * drive) / 20.0f);
    const float tone  = std::clamp(mTone.load(), 0.0f, 100.0f) / 100.0f;
    const float fc    = 600.0f * std::pow(10.0f, tone); // 600 Hz … 6 kHz
    const float lpK   = 1.0f - std::exp(-2.0f * kPi * fc / fs);
    const float level = std::pow(10.0f, mLevelDb.load() / 20.0f) * 0.5f; // makeup for the summed path

    // One-pole high-pass at 720 Hz feeding the clipper.
    const float hpA = std::exp(-2.0f * kPi * 720.0f / fs);

    float x1 = mHpX1[ch], y1 = mHpY1[ch], lp = mLp[ch];
    for (int i = 0; i < n; ++i)
    {
        const float x  = buf[i];
        const float hp = hpA * (y1 + x - x1);
        x1 = x;
        y1 = hp;
        const float y = x + softClip(hp * gain);
        lp += lpK * (y - lp);
        buf[i] = lp * level;
    }
    mHpX1[ch] = x1;
    mHpY1[ch] = y1;
    mLp[ch]   = lp;
}
