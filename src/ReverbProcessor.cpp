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
    double sr = mSampleRate;
    double scale = sr / 44100.0;
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

void ReverbProcessor::setSampleRate(double sr)
{
    mSampleRate = sr;
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

void ReverbProcessor::processBlockStereo(float* L, float* R, int nframes)
{
    // Map parameters
    const float roomSize = mRoomSize.load() / 100.0f;
    const float damping  = mDamping.load()  / 100.0f;
    const float width    = mWidth.load()    / 100.0f;
    const float mix      = mMix.load()      / 100.0f;

    // Freeverb feedback/damping coefficients
    const float feedback = 0.28f + roomSize * 0.7f;
    const float damp     = damping * 0.4f;

    for (int i = 0; i < nframes; ++i)
    {
        float inMono = (L[i] + R[i]) * 0.015f; // attenuate to prevent clip

        // Run comb filters in parallel
        float outL = 0.0f, outR = 0.0f;
        for (int c = 0; c < kNumCombs; ++c)
        {
            outL += mCombL[c].process(inMono, feedback, damp);
            outR += mCombR[c].process(inMono, feedback, damp);
        }

        // Run all-pass filters in series
        for (int a = 0; a < kNumAllPasses; ++a)
        {
            outL = mApL[a].process(outL);
            outR = mApR[a].process(outR);
        }

        // Width: blend L and R wet signals
        float wet1 = width * 0.5f + 0.5f;
        float wet2 = (1.0f - width) * 0.5f;
        float wetL = outL * wet1 + outR * wet2;
        float wetR = outR * wet1 + outL * wet2;

        L[i] = L[i] * (1.0f - mix) + wetL * mix;
        R[i] = R[i] * (1.0f - mix) + wetR * mix;
    }
}

void ReverbProcessor::processBlock(float* buf, int nframes)
{
    // Mono: run L path only
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
