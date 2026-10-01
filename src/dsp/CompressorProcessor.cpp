#include "CompressorProcessor.h"
#include <cmath>
#include <algorithm>

static constexpr float kDbPerOctave = 6.0205999f; // 20·log10(2)

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
        // 20·log10(x) = (20·log10 2)·log2(x); log2/exp2 are much cheaper
        // than log10/pow per sample.
        float level   = std::abs(buf[i]);
        float levelDB = (level > 1e-9f) ? kDbPerOctave * std::log2(level) : -120.0f;

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
        buf[i] *= std::exp2((gainDB + makeup) * (1.0f / kDbPerOctave));
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
