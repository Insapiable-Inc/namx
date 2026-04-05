#include "BitCrusherProcessor.h"
#include <cmath>

static inline float crush(float x, float steps)
{
    return std::round(x * steps) / steps;
}

void BitCrusherProcessor::processBlock(float* buf, int nframes)
{
    const float bits  = mBits.load();
    const float mix   = mMix.load() / 100.0f;
    const float steps = std::pow(2.0f, bits - 1.0f);

    for (int i = 0; i < nframes; ++i)
    {
        float dry = buf[i];
        buf[i] = crush(dry, steps) * mix + dry * (1.0f - mix);
    }
}

void BitCrusherProcessor::processBlockStereo(float* L, float* R, int nframes)
{
    const float bits  = mBits.load();
    const float mix   = mMix.load() / 100.0f;
    const float steps = std::pow(2.0f, bits - 1.0f);

    for (int i = 0; i < nframes; ++i)
    {
        float dryL = L[i];
        float dryR = R[i];
        L[i] = crush(dryL, steps) * mix + dryL * (1.0f - mix);
        R[i] = crush(dryR, steps) * mix + dryR * (1.0f - mix);
    }
}
