#include "ReverbProcessor.h"
#include <cmath>

constexpr int ReverbProcessor::kCombLengths[kNumCombs];
constexpr int ReverbProcessor::kApLengths[kNumAllPasses];

ReverbProcessor::ReverbProcessor()
{
    allocate();
}

void ReverbProcessor::allocate()
{
    double scale = mSampleRate / 44100.0;
    for (int i = 0; i < kNumCombs; ++i)
    {
        mCombL[i].resize(static_cast<int>(kCombLengths[i] * scale));
        mCombR[i].resize(static_cast<int>((kCombLengths[i] + kStereoOffset) * scale));
    }
    for (int i = 0; i < kNumAllPasses; ++i)
    {
        mApL[i].resize(static_cast<int>(kApLengths[i] * scale));
        mApR[i].resize(static_cast<int>((kApLengths[i] + kStereoOffset) * scale));
    }
}

void ReverbProcessor::onSampleRateChanged(double /*sr*/)
{
    allocate();
    reset();
}

void ReverbProcessor::reset()
{
    for (auto& c : mCombL) c.reset();
    for (auto& c : mCombR) c.reset();
    for (auto& a : mApL)   a.reset();
    for (auto& a : mApR)   a.reset();
}

void ReverbProcessor::processBlockStereoOut(float* L, float* R, int nframes)
{
    const float roomSize = mRoomSize.load() / 100.0f;
    const float damping  = mDamping.load()  / 100.0f;
    const float width    = mWidth.load()    / 100.0f;
    const float mix      = mMix.load()      / 100.0f;
    const float feedback = 0.28f + roomSize * 0.7f;
    const float damp     = damping * 0.4f;

    for (int i = 0; i < nframes; ++i)
    {
        float inMono = (L[i] + R[i]) * 0.015f;

        float outL = 0.0f, outR = 0.0f;
        for (int c = 0; c < kNumCombs; ++c)
        {
            outL += mCombL[c].process(inMono, feedback, damp);
            outR += mCombR[c].process(inMono, feedback, damp);
        }
        for (int a = 0; a < kNumAllPasses; ++a)
        {
            outL = mApL[a].process(outL);
            outR = mApR[a].process(outR);
        }

        float wet1 = width * 0.5f + 0.5f;
        float wet2 = (1.0f - width) * 0.5f;
        L[i] = L[i] * (1.0f - mix) + (outL * wet1 + outR * wet2) * mix;
        R[i] = R[i] * (1.0f - mix) + (outR * wet1 + outL * wet2) * mix;
    }
}

void ReverbProcessor::processBlockMono(float* buf, int nframes)
{
    const float roomSize = mRoomSize.load() / 100.0f;
    const float damping  = mDamping.load()  / 100.0f;
    const float mix      = mMix.load()      / 100.0f;
    const float feedback = 0.28f + roomSize * 0.7f;
    const float damp     = damping * 0.4f;

    for (int i = 0; i < nframes; ++i)
    {
        float inSig = buf[i] * 0.015f;
        float out   = 0.0f;
        for (int c = 0; c < kNumCombs; ++c)
            out += mCombL[c].process(inSig, feedback, damp);
        for (int a = 0; a < kNumAllPasses; ++a)
            out = mApL[a].process(out);
        buf[i] = buf[i] * (1.0f - mix) + out * mix;
    }
}
