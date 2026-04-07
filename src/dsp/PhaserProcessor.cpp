#include "PhaserProcessor.h"
#include <cmath>

static constexpr float kPi = 3.14159265358979323846f;

void PhaserProcessor::reset()
{
    mX1L.fill(0.f); mY1L.fill(0.f);
    mX1R.fill(0.f); mY1R.fill(0.f);
    mFbStateL = mFbStateR = 0.0f;
    mPhase    = 0.0f;
}

float PhaserProcessor::processChannel(float in, float phase,
    std::array<float, kStages>& x1,
    std::array<float, kStages>& y1,
    float& fbState,
    float depth, float fbAmt, float mix) const
{
    float lfo = 0.5f * (1.0f + std::sin(2.0f * kPi * phase));
    float fc  = kMinFreq * std::pow(kMaxFreq / kMinFreq, depth * lfo);

    float w = std::tan(kPi * fc / static_cast<float>(mSampleRate));
    float d = (w - 1.0f) / (w + 1.0f);

    float sig = in + fbAmt * fbState;

    for (int s = 0; s < kStages; ++s)
    {
        float y = d * (sig - y1[s]) + x1[s];
        x1[s] = sig;
        y1[s] = y;
        sig   = y;
    }

    fbState = sig;
    return in * (1.0f - mix) + sig * mix;
}

void PhaserProcessor::processBlockMono(float* buf, int nframes)
{
    const float depth = mDepth.load()    / 100.0f;
    const float fbAmt = mFeedback.load() / 100.0f;
    const float mix   = mMix.load()      / 100.0f;
    const float phInc = mRate.load()     / static_cast<float>(mSampleRate);

    for (int i = 0; i < nframes; ++i)
    {
        buf[i] = processChannel(buf[i], mPhase, mX1L, mY1L, mFbStateL, depth, fbAmt, mix);
        mPhase += phInc;
        if (mPhase >= 1.0f) mPhase -= 1.0f;
    }
}

void PhaserProcessor::processBlockStereoOut(float* L, float* R, int nframes)
{
    const float depth = mDepth.load()    / 100.0f;
    const float fbAmt = mFeedback.load() / 100.0f;
    const float mix   = mMix.load()      / 100.0f;
    const float phInc = mRate.load()     / static_cast<float>(mSampleRate);

    for (int i = 0; i < nframes; ++i)
    {
        L[i] = processChannel(L[i], mPhase,        mX1L, mY1L, mFbStateL, depth, fbAmt, mix);
        R[i] = processChannel(R[i], mPhase + 0.5f, mX1R, mY1R, mFbStateR, depth, fbAmt, mix);
        mPhase += phInc;
        if (mPhase >= 1.0f) mPhase -= 1.0f;
    }
}
