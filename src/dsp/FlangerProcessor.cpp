#include "FlangerProcessor.h"

#include <algorithm>
#include <cmath>

static constexpr float kPi = 3.14159265358979323846f;

void FlangerProcessor::onSampleRateChanged(double sr) {
    const size_t length = static_cast<size_t>((kBaseMs + kDepthMs + 5.0f) * sr / 1000.0f) + 4;
    mLineL.resize(length);
    mLineR.resize(length);
    reset();
}

void FlangerProcessor::reset() {
    mLineL.clear();
    mLineR.clear();
    mFbL = mFbR = 0.0f;
    mPhase = 0.0f;
}

void FlangerProcessor::processSample(float in, float phase, DelayLine& line, float& fb, float depth,
                                     float fbAmt, float mix, float& out) const {
    float lfo = std::sin(2.0f * kPi * phase);
    // Range scales the sweep centre and width together (max = kBaseMs/kDepthMs).
    const float r = std::clamp(mRange.load(std::memory_order_relaxed), 0.0f, 100.0f) / 100.0f;
    const float base = 1.5f + (kBaseMs - 1.5f) * r;
    const float span = 1.5f + (kDepthMs - 1.5f) * r;
    float delaySam =
        std::max(1.0f, (base + depth * span * lfo) * static_cast<float>(mSampleRate) / 1000.0f);

    line.push(in + fbAmt * fb);
    const float wet = line.interpolate(delaySam);
    fb = wet;

    out = in * (1.0f - mix) + wet * mix;
}

void FlangerProcessor::processBlockMono(float* buf, int nframes) {
    const float depth = mDepth.load() / 100.0f;
    const float fbAmt = mFeedback.load() / 100.0f;
    const float mix = mMix.load() / 100.0f;
    const float phInc = mRate.load() / static_cast<float>(mSampleRate);

    for (int i = 0; i < nframes; ++i) {
        float out;
        processSample(buf[i], mPhase, mLineL, mFbL, depth, fbAmt, mix, out);
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
        processSample(L[i], mPhase, mLineL, mFbL, depth, fbAmt, mix, outL);
        processSample(R[i], mPhase, mLineR, mFbR, depth, fbAmt, mix, outR);
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
        processSample(L[i], mPhase, mLineL, mFbL, depth, fbAmt, mix, outL);
        processSample(R[i], mPhase + 0.5f, mLineR, mFbR, depth, fbAmt, mix, outR);
        L[i] = outL;
        R[i] = outR;

        mPhase += phInc;
        if (mPhase >= 1.0f) {
            mPhase -= 1.0f;
        }
    }
}
