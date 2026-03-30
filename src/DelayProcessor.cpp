#include "DelayProcessor.h"
#include <algorithm>
#include <cmath>

void DelayProcessor::setSampleRate(double sr)
{
    mSampleRate = sr;
    int maxSamples = static_cast<int>(kMaxDelayMs * sr / 1000.0) + 4;
    mBufL.assign(maxSamples, 0.0f);
    mBufR.assign(maxSamples, 0.0f);
    reset();
}

void DelayProcessor::reset()
{
    std::fill(mBufL.begin(), mBufL.end(), 0.0f);
    std::fill(mBufR.begin(), mBufR.end(), 0.0f);
    mWriteL = mWriteR = 0;
}

void DelayProcessor::processBlock(float* buf, int nframes)
{
    const float timeMS = mTimeMS.load();
    const float fb     = std::min(mFeedback.load() / 100.0f, 0.99f);
    const float mix    = mMix.load() / 100.0f;
    const int   sz     = static_cast<int>(mBufL.size());
    const int   delay  = std::min(static_cast<int>(timeMS * mSampleRate / 1000.0), sz - 1);

    for (int i = 0; i < nframes; ++i)
    {
        int readPos = (mWriteL - delay + sz) % sz;
        float delayed = mBufL[readPos];
        mBufL[mWriteL] = buf[i] + fb * delayed;
        buf[i] = buf[i] * (1.0f - mix) + delayed * mix;
        mWriteL = (mWriteL + 1) % sz;
    }
}

void DelayProcessor::processBlockStereo(float* L, float* R, int nframes)
{
    const float timeMS = mTimeMS.load();
    const float fb     = std::min(mFeedback.load() / 100.0f, 0.99f);
    const float mix    = mMix.load() / 100.0f;
    const int   sz     = static_cast<int>(mBufL.size());
    const int   delay  = std::min(static_cast<int>(timeMS * mSampleRate / 1000.0), sz - 1);

    for (int i = 0; i < nframes; ++i)
    {
        int readPosL = (mWriteL - delay + sz) % sz;
        int readPosR = (mWriteR - delay + sz) % sz;

        float delayedL = mBufL[readPosL];
        float delayedR = mBufR[readPosR];

        // Ping-pong: L feeds into R buffer, R feeds into L buffer
        mBufL[mWriteL] = L[i] + fb * delayedR;
        mBufR[mWriteR] = R[i] + fb * delayedL;

        L[i] = L[i] * (1.0f - mix) + delayedL * mix;
        R[i] = R[i] * (1.0f - mix) + delayedR * mix;

        mWriteL = (mWriteL + 1) % sz;
        mWriteR = (mWriteR + 1) % sz;
    }
}
