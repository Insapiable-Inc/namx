#include "ChorusProcessor.h"
#include <cmath>

static constexpr float kPi = 3.14159265358979323846f;

void ChorusProcessor::onSampleRateChanged(double sr)
{
    int bufSamples = static_cast<int>((kBaseMs + kMaxDepthMs + 5.0f) * sr / 1000.0f) + 4;
    mBufL.assign(bufSamples, 0.0f);
    mBufR.assign(bufSamples, 0.0f);
    reset();
}

void ChorusProcessor::reset()
{
    std::fill(mBufL.begin(), mBufL.end(), 0.0f);
    std::fill(mBufR.begin(), mBufR.end(), 0.0f);
    mWriteL = mWriteR = 0;
    mPhase  = 0.0f;
}

float ChorusProcessor::readInterp(const std::vector<float>& buf, int writePos, float delaySamples) const
{
    int   sz    = static_cast<int>(buf.size());
    float readF = static_cast<float>(writePos) - delaySamples;
    while (readF < 0.0f) readF += sz;

    int   r0   = static_cast<int>(readF) % sz;
    int   r1   = (r0 + 1) % sz;
    float frac = readF - std::floor(readF);
    return buf[r0] * (1.0f - frac) + buf[r1] * frac;
}

void ChorusProcessor::processBlockMono(float* buf, int nframes)
{
    const float rate  = mRate.load();
    const float depth = mDepth.load() / 100.0f;
    const float mix   = mMix.load()   / 100.0f;
    const float phInc = rate / static_cast<float>(mSampleRate);
    const float fs    = static_cast<float>(mSampleRate);
    const int   sz    = static_cast<int>(mBufL.size());

    for (int i = 0; i < nframes; ++i)
    {
        float lfo     = std::sin(2.0f * kPi * mPhase);
        float delayMs = kBaseMs + depth * kMaxDepthMs * lfo;
        float delaySamples = std::max(1.0f, delayMs * fs / 1000.0f);

        mBufL[mWriteL] = buf[i];
        float wet = readInterp(mBufL, mWriteL, delaySamples);
        buf[i] = buf[i] * (1.0f - mix) + wet * mix;
        mWriteL = (mWriteL + 1) % sz;

        mPhase += phInc;
        if (mPhase >= 1.0f) mPhase -= 1.0f;
    }
}

void ChorusProcessor::processBlockDual(float* L, float* R, int nframes)
{
    const float rate  = mRate.load();
    const float depth = mDepth.load() / 100.0f;
    const float mix   = mMix.load()   / 100.0f;
    const float phInc = rate / static_cast<float>(mSampleRate);
    const float fs    = static_cast<float>(mSampleRate);
    const int   szL   = static_cast<int>(mBufL.size());
    const int   szR   = static_cast<int>(mBufR.size());

    for (int i = 0; i < nframes; ++i)
    {
        // Same LFO for both channels: identical mono effect applied per channel.
        float lfo = std::sin(2.0f * kPi * mPhase);
        float del = std::max(1.0f, (kBaseMs + depth * kMaxDepthMs * lfo) * fs / 1000.0f);

        mBufL[mWriteL] = L[i];
        float wetL = readInterp(mBufL, mWriteL, del);
        L[i] = L[i] * (1.0f - mix) + wetL * mix;
        mWriteL = (mWriteL + 1) % szL;

        mBufR[mWriteR] = R[i];
        float wetR = readInterp(mBufR, mWriteR, del);
        R[i] = R[i] * (1.0f - mix) + wetR * mix;
        mWriteR = (mWriteR + 1) % szR;

        mPhase += phInc;
        if (mPhase >= 1.0f) mPhase -= 1.0f;
    }
}

void ChorusProcessor::processBlockStereoOut(float* L, float* R, int nframes)
{
    const float rate  = mRate.load();
    const float depth = mDepth.load() / 100.0f;
    const float mix   = mMix.load()   / 100.0f;
    const float phInc = rate / static_cast<float>(mSampleRate);
    const float fs    = static_cast<float>(mSampleRate);
    const int   szL   = static_cast<int>(mBufL.size());
    const int   szR   = static_cast<int>(mBufR.size());

    for (int i = 0; i < nframes; ++i)
    {
        float lfoL = std::sin(2.0f * kPi * mPhase);
        float lfoR = std::sin(2.0f * kPi * (mPhase + 0.5f)); // 180° apart

        float delL = std::max(1.0f, (kBaseMs + depth * kMaxDepthMs * lfoL) * fs / 1000.0f);
        float delR = std::max(1.0f, (kBaseMs + depth * kMaxDepthMs * lfoR) * fs / 1000.0f);

        mBufL[mWriteL] = L[i];
        float wetL = readInterp(mBufL, mWriteL, delL);
        L[i] = L[i] * (1.0f - mix) + wetL * mix;
        mWriteL = (mWriteL + 1) % szL;

        mBufR[mWriteR] = R[i];
        float wetR = readInterp(mBufR, mWriteR, delR);
        R[i] = R[i] * (1.0f - mix) + wetR * mix;
        mWriteR = (mWriteR + 1) % szR;

        mPhase += phInc;
        if (mPhase >= 1.0f) mPhase -= 1.0f;
    }
}
