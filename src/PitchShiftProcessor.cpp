#include "PitchShiftProcessor.h"
#include <cmath>
#include <algorithm>

static constexpr float kPi = 3.14159265358979323846f;

void PitchShiftProcessor::setSampleRate(double sr)
{
    mSampleRate = sr;
    mBuf.assign(kBufSize, 0.0f);
    reset();
}

void PitchShiftProcessor::reset()
{
    std::fill(mBuf.begin(), mBuf.end(), 0.0f);
    mWritePos = 0;
    mPhaseA   = 0;
    mPhaseB   = kGrainSize / 2;
    mReadPosA = static_cast<float>((kBufSize - kGrainSize) % kBufSize);
    mReadPosB = static_cast<float>((kBufSize - kGrainSize + kGrainSize / 2) % kBufSize);
}

float PitchShiftProcessor::readInterp(float pos) const
{
    // Wrap position into [0, kBufSize)
    while (pos < 0.f)      pos += kBufSize;
    while (pos >= kBufSize) pos -= kBufSize;

    int   r0   = static_cast<int>(pos) % kBufSize;
    int   r1   = (r0 + 1) % kBufSize;
    float frac = pos - std::floor(pos);
    return mBuf[r0] * (1.0f - frac) + mBuf[r1] * frac;
}

void PitchShiftProcessor::processBlock(float* buf, int nframes)
{
    const float semitones = std::round(mSemitones.load());
    const float mix       = mMix.load() / 100.0f;

    if (std::abs(mix) < 1e-4f && std::abs(semitones) < 0.5f)
        return; // fully dry and no shift

    const float ratio = std::pow(2.0f, semitones / 12.0f);

    for (int i = 0; i < nframes; ++i)
    {
        const float dry = buf[i];

        // Write input into circular buffer
        mBuf[mWritePos] = dry;

        // --- Grain A ---
        // Hann window: w = 0.5*(1 - cos(2*pi*phase/G))
        float winA = 0.5f * (1.0f - std::cos(2.0f * kPi * mPhaseA / kGrainSize));
        float smpA = readInterp(mReadPosA) * winA;

        // --- Grain B (180° offset) ---
        float winB = 0.5f * (1.0f - std::cos(2.0f * kPi * mPhaseB / kGrainSize));
        float smpB = readInterp(mReadPosB) * winB;

        float wet = smpA + smpB;

        buf[i] = dry * (1.0f - mix) + wet * mix;

        // Advance write pointer
        mWritePos = (mWritePos + 1) % kBufSize;

        // Advance read pointers at pitch ratio speed
        mReadPosA += ratio;
        if (mReadPosA >= kBufSize) mReadPosA -= kBufSize;

        mReadPosB += ratio;
        if (mReadPosB >= kBufSize) mReadPosB -= kBufSize;

        // Advance grain phases
        ++mPhaseA;
        ++mPhaseB;

        // Reset grain A when it completes: jump read pointer back to
        // kGrainSize samples behind write pointer (re-syncs with input)
        if (mPhaseA >= kGrainSize)
        {
            mPhaseA   = 0;
            mReadPosA = static_cast<float>((mWritePos - kGrainSize + kBufSize) % kBufSize);
        }

        // Reset grain B
        if (mPhaseB >= kGrainSize)
        {
            mPhaseB   = 0;
            mReadPosB = static_cast<float>((mWritePos - kGrainSize + kBufSize) % kBufSize);
        }
    }
}
