#include "CompressorProcessor.h"
#include <cmath>
#include <algorithm>

void CompressorProcessor::reset()
{
    mEnvDB[0]  = mEnvDB[1]  = -120.0f;
    mGainDB[0] = mGainDB[1] =    0.0f;
}

void CompressorProcessor::processChannel(float* buf, int nframes, int ch)
{
    const float threshold = mThresholdDB.load();
    const float ratio     = mRatio.load();
    const float makeup    = mMakeupDB.load();
    const float fs        = static_cast<float>(mSampleRate);

    const float atCoeff  = std::exp(-1.0f / (fs * mAttackMS.load()  * 0.001f));
    const float relCoeff = std::exp(-1.0f / (fs * mReleaseMS.load() * 0.001f));

    float& envDB  = mEnvDB[ch];
    float& gainDB = mGainDB[ch];
    float  minGainDB = 0.0f;

    for (int i = 0; i < nframes; ++i)
    {
        float level   = std::abs(buf[i]);
        float levelDB = (level > 1e-9f) ? 20.0f * std::log10(level) : -120.0f;

        if (levelDB > envDB)
            envDB = atCoeff  * envDB + (1.0f - atCoeff)  * levelDB;
        else
            envDB = relCoeff * envDB + (1.0f - relCoeff) * levelDB;

        float targetGR = 0.0f;
        if (envDB > threshold)
            targetGR = (threshold - envDB) * (1.0f - 1.0f / ratio);

        if (targetGR < gainDB)
            gainDB = atCoeff  * gainDB + (1.0f - atCoeff)  * targetGR;
        else
            gainDB = relCoeff * gainDB + (1.0f - relCoeff) * targetGR;

        minGainDB = std::min(minGainDB, gainDB);
        buf[i] *= std::pow(10.0f, (gainDB + makeup) / 20.0f);
    }
    if (-minGainDB > mGrPeakDB.load(std::memory_order_relaxed))
        mGrPeakDB.store(-minGainDB, std::memory_order_relaxed);
}

void CompressorProcessor::processBlockMono(float* buf, int nframes)
{
    processChannel(buf, nframes, 0);
}

void CompressorProcessor::processBlockDual(float* L, float* R, int nframes)
{
    processChannel(L, nframes, 0);
    processChannel(R, nframes, 1);
}
