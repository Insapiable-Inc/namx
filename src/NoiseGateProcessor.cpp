#include "NoiseGateProcessor.h"
#include <cmath>

void NoiseGateProcessor::setSampleRate(double sr)
{
    mSampleRate = sr;
    reset();
}

void NoiseGateProcessor::reset()
{
    mEnvelope = 0.0f;
    mGain     = 0.0f;
}

void NoiseGateProcessor::processBlock(float* buf, int nframes)
{
    const float threshLin = std::pow(10.0f, mThresholdDB.load() / 20.0f);
    const float fs        = static_cast<float>(mSampleRate);
    const float atMS      = mAttackMS.load();
    const float relMS     = mReleaseMS.load();

    const float atCoeff  = std::exp(-1.0f / (fs * atMS  * 0.001f));
    const float relCoeff = std::exp(-1.0f / (fs * relMS * 0.001f));

    for (int i = 0; i < nframes; ++i)
    {
        // Peak envelope follower
        float level = std::abs(buf[i]);
        if (level > mEnvelope)
            mEnvelope = atCoeff  * mEnvelope + (1.0f - atCoeff)  * level;
        else
            mEnvelope = relCoeff * mEnvelope + (1.0f - relCoeff) * level;

        // Gate: open when above threshold, close below
        float target = (mEnvelope >= threshLin) ? 1.0f : 0.0f;

        // Smooth gate transitions
        if (target > mGain)
            mGain = atCoeff  * mGain + (1.0f - atCoeff)  * target;
        else
            mGain = relCoeff * mGain + (1.0f - relCoeff) * target;

        buf[i] *= mGain;
    }
}
