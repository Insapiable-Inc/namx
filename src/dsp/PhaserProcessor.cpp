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

float PhaserProcessor::coeffAt(float phase, float depth) const
{
    float lfo = 0.5f * (1.0f + std::sin(2.0f * kPi * phase));
    float fc  = kMinFreq * std::pow(kMaxFreq / kMinFreq, depth * lfo);
    float w   = std::tan(kPi * fc / static_cast<float>(mSampleRate));
    return (w - 1.0f) / (w + 1.0f);
}

float PhaserProcessor::processChannel(float in, float d,
    std::array<float, kStages>& x1,
    std::array<float, kStages>& y1,
    float& fbState, float fbAmt, float mix) const
{
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

    float d = 0.0f;
    for (int i = 0; i < nframes; ++i)
    {
        if (i % kCtrlInterval == 0) d = coeffAt(mPhase, depth);
        buf[i] = processChannel(buf[i], d, mX1L, mY1L, mFbStateL, fbAmt, mix);
        mPhase += phInc;
        if (mPhase >= 1.0f) mPhase -= 1.0f;
    }
}

void PhaserProcessor::processBlockDual(float* L, float* R, int nframes)
{
    const float depth = mDepth.load()    / 100.0f;
    const float fbAmt = mFeedback.load() / 100.0f;
    const float mix   = mMix.load()      / 100.0f;
    const float phInc = mRate.load()     / static_cast<float>(mSampleRate);

    float d = 0.0f;
    for (int i = 0; i < nframes; ++i)
    {
        // Same LFO phase for both channels: identical mono effect per channel.
        if (i % kCtrlInterval == 0) d = coeffAt(mPhase, depth);
        L[i] = processChannel(L[i], d, mX1L, mY1L, mFbStateL, fbAmt, mix);
        R[i] = processChannel(R[i], d, mX1R, mY1R, mFbStateR, fbAmt, mix);
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

    float dL = 0.0f, dR = 0.0f;
    for (int i = 0; i < nframes; ++i)
    {
        if (i % kCtrlInterval == 0)
        {
            dL = coeffAt(mPhase, depth);
            dR = coeffAt(mPhase + 0.5f, depth); // 180° apart
        }
        L[i] = processChannel(L[i], dL, mX1L, mY1L, mFbStateL, fbAmt, mix);
        R[i] = processChannel(R[i], dR, mX1R, mY1R, mFbStateR, fbAmt, mix);
        mPhase += phInc;
        if (mPhase >= 1.0f) mPhase -= 1.0f;
    }
}
