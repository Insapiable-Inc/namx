#include "TremoloProcessor.h"
#include <cmath>

static constexpr float kPi = 3.14159265358979323846f;

void TremoloProcessor::processBlock(float* buf, int nframes)
{
    const float rate  = mRate.load();
    const float depth = mDepth.load() / 100.0f;
    const float phInc = rate / static_cast<float>(mSampleRate);

    for (int i = 0; i < nframes; ++i)
    {
        // LFO goes -1 to +1; scale to gain 1-depth to 1
        float lfo  = std::sin(2.0f * kPi * mPhase);
        float gain = 1.0f - depth * (1.0f - lfo) * 0.5f;
        buf[i] *= gain;

        mPhase += phInc;
        if (mPhase >= 1.0f) mPhase -= 1.0f;
    }
}

void TremoloProcessor::processBlockStereo(float* L, float* R, int nframes)
{
    const float rate  = mRate.load();
    const float depth = mDepth.load() / 100.0f;
    const float phInc = rate / static_cast<float>(mSampleRate);

    for (int i = 0; i < nframes; ++i)
    {
        // L and R: 90° apart for gentle auto-pan effect
        float lfoL  = std::sin(2.0f * kPi * mPhase);
        float lfoR  = std::sin(2.0f * kPi * (mPhase + 0.25f));
        float gainL = 1.0f - depth * (1.0f - lfoL) * 0.5f;
        float gainR = 1.0f - depth * (1.0f - lfoR) * 0.5f;
        L[i] *= gainL;
        R[i] *= gainR;

        mPhase += phInc;
        if (mPhase >= 1.0f) mPhase -= 1.0f;
    }
}
