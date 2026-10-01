#include "TremoloProcessor.h"

#include <cmath>

static constexpr float kPi = 3.14159265358979323846f;

void TremoloProcessor::processBlockMono(float* buf, int nframes) {
    const float rate = mRate.load();
    const float depth = mDepth.load() / 100.0f;
    const float phInc = rate / static_cast<float>(mSampleRate);

    for (int i = 0; i < nframes; ++i) {
        float lfo = std::sin(2.0f * kPi * mPhase);
        float gain = 1.0f - depth * (1.0f - lfo) * 0.5f;
        buf[i] *= gain;

        mPhase += phInc;
        if (mPhase >= 1.0f) {
            mPhase -= 1.0f;
        }
    }
}

void TremoloProcessor::processBlockDual(float* L, float* R, int nframes) {
    const float rate = mRate.load();
    const float depth = mDepth.load() / 100.0f;
    const float phInc = rate / static_cast<float>(mSampleRate);

    for (int i = 0; i < nframes; ++i) {
        // Same LFO gain on both channels: identical mono effect per channel.
        float lfo = std::sin(2.0f * kPi * mPhase);
        float gain = 1.0f - depth * (1.0f - lfo) * 0.5f;
        L[i] *= gain;
        R[i] *= gain;

        mPhase += phInc;
        if (mPhase >= 1.0f) {
            mPhase -= 1.0f;
        }
    }
}

void TremoloProcessor::processBlockStereoOut(float* L, float* R, int nframes) {
    const float rate = mRate.load();
    const float depth = mDepth.load() / 100.0f;
    const float phInc = rate / static_cast<float>(mSampleRate);

    for (int i = 0; i < nframes; ++i) {
        float lfoL = std::sin(2.0f * kPi * mPhase);
        float lfoR = std::sin(2.0f * kPi * (mPhase + 0.5f)); // 180° apart: true auto-pan
        L[i] *= 1.0f - depth * (1.0f - lfoL) * 0.5f;
        R[i] *= 1.0f - depth * (1.0f - lfoR) * 0.5f;

        mPhase += phInc;
        if (mPhase >= 1.0f) {
            mPhase -= 1.0f;
        }
    }
}
