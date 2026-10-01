#include "DelayProcessor.h"
#include <algorithm>
#include <cmath>

void DelayProcessor::onSampleRateChanged(double sr)
{
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

void DelayProcessor::processBlockMono(float* buf, int nframes)
{
    const float fb    = std::min(mFeedback.load() / 100.0f, 0.99f);
    const float mix   = mMix.load() / 100.0f;
    const int   sz    = static_cast<int>(mBufL.size());
    const int   delay = std::min(static_cast<int>(mTimeMS.load() * mSampleRate / 1000.0), sz - 1);

    for (int i = 0; i < nframes; ++i)
    {
        int   readPos = mWriteL - delay;
        if (readPos < 0) readPos += sz;
        float delayed = mBufL[readPos];
        mBufL[mWriteL] = buf[i] + fb * delayed;
        buf[i] = buf[i] * (1.0f - mix) + delayed * mix;
        if (++mWriteL == sz) mWriteL = 0;
    }
}

void DelayProcessor::processBlockDual(float* L, float* R, int nframes)
{
    // Stereo input: independent mono delay per channel (no cross-feedback).
    const float fb    = std::min(mFeedback.load() / 100.0f, 0.99f);
    const float mix   = mMix.load() / 100.0f;
    const int   sz    = static_cast<int>(mBufL.size());
    const int   delay = std::min(static_cast<int>(mTimeMS.load() * mSampleRate / 1000.0), sz - 1);

    for (int i = 0; i < nframes; ++i)
    {
        int   readPosL = mWriteL - delay;
        if (readPosL < 0) readPosL += sz;
        float delayedL = mBufL[readPosL];
        mBufL[mWriteL] = L[i] + fb * delayedL;
        L[i] = L[i] * (1.0f - mix) + delayedL * mix;
        if (++mWriteL == sz) mWriteL = 0;

        int   readPosR = mWriteR - delay;
        if (readPosR < 0) readPosR += sz;
        float delayedR = mBufR[readPosR];
        mBufR[mWriteR] = R[i] + fb * delayedR;
        R[i] = R[i] * (1.0f - mix) + delayedR * mix;
        if (++mWriteR == sz) mWriteR = 0;
    }
}

void DelayProcessor::processBlockStereoOut(float* L, float* R, int nframes)
{
    // Ping-pong: input enters L buffer, echoes alternate L→R→L→…
    const float fb    = std::min(mFeedback.load() / 100.0f, 0.99f);
    const float mix   = mMix.load() / 100.0f;
    const int   sz    = static_cast<int>(mBufL.size());
    const int   delay = std::min(static_cast<int>(mTimeMS.load() * mSampleRate / 1000.0), sz - 1);

    for (int i = 0; i < nframes; ++i)
    {
        int readPosL = mWriteL - delay;
        if (readPosL < 0) readPosL += sz;
        int readPosR = mWriteR - delay;
        if (readPosR < 0) readPosR += sz;

        float delayedL = mBufL[readPosL];
        float delayedR = mBufR[readPosR];

        mBufL[mWriteL] = L[i] + fb * delayedR;
        mBufR[mWriteR] = fb * delayedL;

        L[i] = L[i] * (1.0f - mix) + delayedL * mix;
        R[i] = R[i] * (1.0f - mix) + delayedR * mix;

        if (++mWriteL == sz) mWriteL = 0;
        if (++mWriteR == sz) mWriteR = 0;
    }
}
