#include "ReverseProcessor.h"
#include <algorithm>

void ReverseProcessor::setSampleRate(double sr)
{
    mSampleRate = sr;
    mMaxBufSize = static_cast<int>(2.0 * sr) + 1; // up to 2 s
    for (auto& v : mBufL) v.assign(mMaxBufSize, 0.0f);
    for (auto& v : mBufR) v.assign(mMaxBufSize, 0.0f);
    reset();
}

void ReverseProcessor::reset()
{
    for (auto& v : mBufL) std::fill(v.begin(), v.end(), 0.0f);
    for (auto& v : mBufR) std::fill(v.begin(), v.end(), 0.0f);
    mFillIdx = 0;
    mPhase   = 0;
}

void ReverseProcessor::processBlock(float* buf, int nframes)
{
    const float mix     = mMix.load() / 100.0f;
    const int   bufSize = std::min(
        static_cast<int>(mLength.load() * mSampleRate / 1000.0f),
        mMaxBufSize);

    for (int i = 0; i < nframes; ++i)
    {
        const int playIdx = 1 - mFillIdx;

        mBufL[mFillIdx][mPhase] = buf[i];

        int readPos = bufSize - 1 - mPhase;
        if (readPos < 0) readPos = 0;
        float wet = mBufL[playIdx][readPos];

        buf[i] = wet * mix + buf[i] * (1.0f - mix);

        if (++mPhase >= bufSize)
        {
            mPhase   = 0;
            mFillIdx = playIdx; // swap
        }
    }
}

void ReverseProcessor::processBlockStereo(float* L, float* R, int nframes)
{
    const float mix     = mMix.load() / 100.0f;
    const int   bufSize = std::min(
        static_cast<int>(mLength.load() * mSampleRate / 1000.0f),
        mMaxBufSize);

    for (int i = 0; i < nframes; ++i)
    {
        const int playIdx = 1 - mFillIdx;

        mBufL[mFillIdx][mPhase] = L[i];
        mBufR[mFillIdx][mPhase] = R[i];

        int readPos = bufSize - 1 - mPhase;
        if (readPos < 0) readPos = 0;
        float wetL = mBufL[playIdx][readPos];
        float wetR = mBufR[playIdx][readPos];

        L[i] = wetL * mix + L[i] * (1.0f - mix);
        R[i] = wetR * mix + R[i] * (1.0f - mix);

        if (++mPhase >= bufSize)
        {
            mPhase   = 0;
            mFillIdx = playIdx;
        }
    }
}
