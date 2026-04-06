#include "CompressorProcessor.h"
#include <cmath>
#include <algorithm>

void CompressorProcessor::reset()
{
    mEnvDB  = -120.0f;
    mGainDB =    0.0f;
}

void CompressorProcessor::processBlockMono(float* buf, int nframes)
{
    const float threshold = mThresholdDB.load();
    const float ratio     = mRatio.load();
    const float makeup    = mMakeupDB.load();
    const float fs        = static_cast<float>(mSampleRate);

    const float atCoeff  = std::exp(-1.0f / (fs * mAttackMS.load()  * 0.001f));
    const float relCoeff = std::exp(-1.0f / (fs * mReleaseMS.load() * 0.001f));

    for (int i = 0; i < nframes; ++i)
    {
        float level   = std::abs(buf[i]);
        float levelDB = (level > 1e-9f) ? 20.0f * std::log10(level) : -120.0f;

        if (levelDB > mEnvDB)
            mEnvDB = atCoeff  * mEnvDB + (1.0f - atCoeff)  * levelDB;
        else
            mEnvDB = relCoeff * mEnvDB + (1.0f - relCoeff) * levelDB;

        float targetGR = 0.0f;
        if (mEnvDB > threshold)
            targetGR = (threshold - mEnvDB) * (1.0f - 1.0f / ratio);

        if (targetGR < mGainDB)
            mGainDB = atCoeff  * mGainDB + (1.0f - atCoeff)  * targetGR;
        else
            mGainDB = relCoeff * mGainDB + (1.0f - relCoeff) * targetGR;

        buf[i] *= std::pow(10.0f, (mGainDB + makeup) / 20.0f);
    }
}
