#include "NoiseGateProcessor.h"
#include <algorithm>
#include <cmath>

void NoiseGateProcessor::reset()
{
    mEnvelope[0] = mEnvelope[1] = 0.0f;
    mGain[0]     = mGain[1]     = 0.0f;
}

void NoiseGateProcessor::processChannel(float* buf, int nframes, int ch)
{
    const float threshLin = std::pow(10.0f, mThresholdDB.load() / 20.0f);
    const float fs        = static_cast<float>(mSampleRate);
    const float atMS      = mAttackMS.load();
    const float relMS     = mReleaseMS.load();

    const float atCoeff  = std::exp(-1.0f / (fs * atMS  * 0.001f));
    const float relCoeff = std::exp(-1.0f / (fs * relMS * 0.001f));

    float& env  = mEnvelope[ch];
    float& gain = mGain[ch];
    float  minGain = 1.0f;

    for (int i = 0; i < nframes; ++i)
    {
        float level = std::abs(buf[i]);
        if (level > env)
            env = atCoeff  * env + (1.0f - atCoeff)  * level;
        else
            env = relCoeff * env + (1.0f - relCoeff) * level;

        float target = (env >= threshLin) ? 1.0f : 0.0f;

        if (target > gain)
            gain = atCoeff  * gain + (1.0f - atCoeff)  * target;
        else
            gain = relCoeff * gain + (1.0f - relCoeff) * target;

        minGain = std::min(minGain, gain);
        buf[i] *= gain;
    }
    // One log per block, not per sample.
    const float grDB = minGain > 1e-3f ? -20.0f * std::log10(minGain) : kMaxGrDb;
    if (grDB > mGrPeakDB.load(std::memory_order_relaxed))
        mGrPeakDB.store(std::min(grDB, kMaxGrDb), std::memory_order_relaxed);
}

void NoiseGateProcessor::processBlockMono(float* buf, int nframes)
{
    processChannel(buf, nframes, 0);
}

void NoiseGateProcessor::processBlockDual(float* L, float* R, int nframes)
{
    processChannel(L, nframes, 0);
    processChannel(R, nframes, 1);
}
