#include "FlangerProcessor.h"

#include <algorithm>
#include <cmath>

static constexpr float kPi = 3.14159265358979323846f;

void FlangerProcessor::onSampleRateChanged(double sr) {
    int bufSamples = static_cast<int>((kBaseMs + kDepthMs + 5.0f) * sr / 1000.0f) + 4;
    mBufL.assign(bufSamples, 0.0f);
    mBufR.assign(bufSamples, 0.0f);
    reset();
}

void FlangerProcessor::reset() {
    std::fill(mBufL.begin(), mBufL.end(), 0.0f);
    std::fill(mBufR.begin(), mBufR.end(), 0.0f);
    mWriteL = mWriteR = 0;
    mFbL = mFbR = 0.0f;
    mPhase = 0.0f;
}

float FlangerProcessor::readInterp(const std::vector<float>& buf, int writePos,
                                   float delaySamples) const {
    int sz = static_cast<int>(buf.size());
    float readF = static_cast<float>(writePos) - delaySamples;
    while (readF < 0.0f) {
        readF += sz;
    }

    int r0 = static_cast<int>(readF); // < sz: delaySamples >= 1
    if (r0 >= sz) {
        r0 -= sz;
    }
    int r1 = r0 + 1 == sz ? 0 : r0 + 1;
    float frac = readF - std::floor(readF);
    return buf[r0] * (1.0f - frac) + buf[r1] * frac;
}

void FlangerProcessor::processSample(float in, float phase, std::vector<float>& buf, int& writePos,
                                     float& fb, float depth, float fbAmt, float mix,
                                     float& out) const {
    float lfo = std::sin(2.0f * kPi * phase);
    // Range scales the sweep centre and width together (max = kBaseMs/kDepthMs).
    const float r = std::clamp(mRange.load(std::memory_order_relaxed), 0.0f, 100.0f) / 100.0f;
    const float base = 1.5f + (kBaseMs - 1.5f) * r;
    const float span = 1.5f + (kDepthMs - 1.5f) * r;
    float delaySam =
        std::max(1.0f, (base + depth * span * lfo) * static_cast<float>(mSampleRate) / 1000.0f);

    int sz = static_cast<int>(buf.size());
    buf[writePos] = in + fbAmt * fb;
    float wet = readInterp(buf, writePos, delaySam);
    fb = wet;
    if (++writePos == sz) {
        writePos = 0;
    }

    out = in * (1.0f - mix) + wet * mix;
}

void FlangerProcessor::processBlockMono(float* buf, int nframes) {
    const float depth = mDepth.load() / 100.0f;
    const float fbAmt = mFeedback.load() / 100.0f;
    const float mix = mMix.load() / 100.0f;
    const float phInc = mRate.load() / static_cast<float>(mSampleRate);

    for (int i = 0; i < nframes; ++i) {
        float out;
        processSample(buf[i], mPhase, mBufL, mWriteL, mFbL, depth, fbAmt, mix, out);
        buf[i] = out;

        mPhase += phInc;
        if (mPhase >= 1.0f) {
            mPhase -= 1.0f;
        }
    }
}

void FlangerProcessor::processBlockDual(float* L, float* R, int nframes) {
    const float depth = mDepth.load() / 100.0f;
    const float fbAmt = mFeedback.load() / 100.0f;
    const float mix = mMix.load() / 100.0f;
    const float phInc = mRate.load() / static_cast<float>(mSampleRate);

    for (int i = 0; i < nframes; ++i) {
        // Same LFO phase for both channels: identical mono effect per channel.
        float outL, outR;
        processSample(L[i], mPhase, mBufL, mWriteL, mFbL, depth, fbAmt, mix, outL);
        processSample(R[i], mPhase, mBufR, mWriteR, mFbR, depth, fbAmt, mix, outR);
        L[i] = outL;
        R[i] = outR;

        mPhase += phInc;
        if (mPhase >= 1.0f) {
            mPhase -= 1.0f;
        }
    }
}

void FlangerProcessor::processBlockStereoOut(float* L, float* R, int nframes) {
    const float depth = mDepth.load() / 100.0f;
    const float fbAmt = mFeedback.load() / 100.0f;
    const float mix = mMix.load() / 100.0f;
    const float phInc = mRate.load() / static_cast<float>(mSampleRate);

    for (int i = 0; i < nframes; ++i) {
        float outL, outR;
        processSample(L[i], mPhase, mBufL, mWriteL, mFbL, depth, fbAmt, mix, outL);
        processSample(R[i], mPhase + 0.5f, mBufR, mWriteR, mFbR, depth, fbAmt, mix, outR);
        L[i] = outL;
        R[i] = outR;

        mPhase += phInc;
        if (mPhase >= 1.0f) {
            mPhase -= 1.0f;
        }
    }
}
