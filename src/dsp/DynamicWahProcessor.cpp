#include "DynamicWahProcessor.h"
#include <algorithm>
#include <cmath>

static constexpr float kPi = 3.14159265358979323846f;

void DynamicWahProcessor::onSampleRateChanged(double sr)
{
    mEnvAttack  = std::exp(-1.0 / (0.002 * sr));
    mEnvRelease = std::exp(-1.0 / (0.080 * sr));
    reset();
}

void DynamicWahProcessor::reset()
{
    mEnv    = 0.0f;
    mSVF_LP = 0.0f;
    mSVF_BP = 0.0f;
}

void DynamicWahProcessor::processBlockMono(float* buf, int nframes)
{
    const float sens  = mSensitivity.load() / 100.0f;
    const float res   = mResonance.load()   / 100.0f;
    const float mix   = mMix.load()         / 100.0f;
    const float q_inv = 1.0f / (0.5f + res * 7.5f);
    const float sr    = static_cast<float>(mSampleRate);

    for (int i = 0; i < nframes; ++i)
    {
        const float dry   = buf[i];
        float level = std::abs(dry);
        float coeff = (level > mEnv) ? mEnvAttack : mEnvRelease;
        mEnv += (level - mEnv) * (1.0f - coeff);

        float cutoff = std::max(kBaseFreq, std::min(kBaseFreq + sens * mEnv * (kPeakFreq - kBaseFreq), kPeakFreq));
        float f      = std::min(2.0f * kPi * cutoff / sr, 0.5f);

        float hp = dry - q_inv * mSVF_BP - mSVF_LP;
        mSVF_BP += f * hp;
        mSVF_LP += f * mSVF_BP;

        buf[i] = mSVF_BP * mix + dry * (1.0f - mix);
    }
}
