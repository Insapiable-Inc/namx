#include "DynamicWahProcessor.h"

#include <algorithm>
#include <cmath>

static constexpr float kPi = 3.14159265358979323846f;

void DynamicWahProcessor::onSampleRateChanged(double sr) {
    mEnvAttack = std::exp(-1.0 / (0.002 * sr));
    mEnvRelease = std::exp(-1.0 / (0.080 * sr));
    reset();
}

void DynamicWahProcessor::reset() {
    mEnv[0] = mEnv[1] = 0.0f;
    mSVF_LP[0] = mSVF_LP[1] = 0.0f;
    mSVF_BP[0] = mSVF_BP[1] = 0.0f;
}

void DynamicWahProcessor::processChannel(float* buf, size_t nframes, int ch) {
    const float sens = mSensitivity.load() / 100.0f;
    const float res = mResonance.load() / 100.0f;
    const float mix = mMix.load() / 100.0f;
    const float q_inv = 1.0f / (0.5f + res * 7.5f);
    const float sr = static_cast<float>(mSampleRate);

    const bool pedal = mPedal.load(std::memory_order_relaxed);
    const float posTgt =
        std::clamp(mPosition.load(std::memory_order_relaxed), 0.0f, 100.0f) / 100.0f;
    const float posK = 1.0f - std::exp(-1.0f / (0.015f * sr)); // ~15 ms pedal glide
    float& pos = mPosSmooth[ch];

    float& env = mEnv[ch];
    float& lp = mSVF_LP[ch];
    float& bp = mSVF_BP[ch];

    for (size_t i = 0; i < nframes; ++i) {
        const float dry = buf[i];
        float level = std::abs(dry);
        float coeff = (level > env) ? mEnvAttack : mEnvRelease;
        env += (level - env) * (1.0f - coeff);

        float cutoff;
        if (pedal) {
            // Rocker travel maps exponentially, like a real wah's pot taper.
            pos += posK * (posTgt - pos);
            cutoff = kBaseFreq * std::pow(kPeakFreq / kBaseFreq, pos);
        } else {
            cutoff = std::max(
                kBaseFreq, std::min(kBaseFreq + sens * env * (kPeakFreq - kBaseFreq), kPeakFreq));
        }
        float f = std::min(2.0f * kPi * cutoff / sr, 0.5f);

        float hp = dry - q_inv * bp - lp;
        bp += f * hp;
        lp += f * bp;

        buf[i] = bp * mix + dry * (1.0f - mix);
    }
}

void DynamicWahProcessor::processBlockMono(float* buf, size_t nframes) {
    processChannel(buf, nframes, 0);
}

void DynamicWahProcessor::processBlockDual(float* L, float* R, size_t nframes) {
    processChannel(L, nframes, 0);
    processChannel(R, nframes, 1);
}
