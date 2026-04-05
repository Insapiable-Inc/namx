#include "DynamicWahProcessor.h"
#include <cmath>
#include <algorithm>

static constexpr float kPi = 3.14159265358979323846f;

void DynamicWahProcessor::setSampleRate(double sr)
{
    mSampleRate = sr;
    // Envelope: ~2 ms attack, ~80 ms release
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

void DynamicWahProcessor::processBlock(float* buf, int nframes)
{
    const float sens = mSensitivity.load() / 100.0f;
    const float res  = mResonance.load()   / 100.0f;
    const float mix  = mMix.load()         / 100.0f;

    // Q range: at res=0 → Q≈0.5 (wide, no resonance)
    //          at res=1 → Q≈8.0 (narrow, pronounced peak)
    const float q_inv = 1.0f / (0.5f + res * 7.5f);  // 1/Q

    const float sr = static_cast<float>(mSampleRate);

    for (int i = 0; i < nframes; ++i)
    {
        const float dry = buf[i];

        // Envelope follower
        float level = std::abs(dry);
        float coeff = (level > mEnv) ? mEnvAttack : mEnvRelease;
        mEnv += (level - mEnv) * (1.0f - coeff);

        // Cutoff frequency: base + env-driven sweep
        float cutoff = kBaseFreq + sens * mEnv * (kPeakFreq - kBaseFreq);
        cutoff = std::max(kBaseFreq, std::min(cutoff, kPeakFreq));

        // Chamberlin SVF coefficient (linear approximation, stable below sr/6)
        float f = 2.0f * kPi * cutoff / sr;
        f = std::min(f, 0.5f); // stability cap

        // SVF update
        float hp  = dry - q_inv * mSVF_BP - mSVF_LP;
        mSVF_BP  += f * hp;
        mSVF_LP  += f * mSVF_BP;

        // Bandpass output gives the classic wah tone
        buf[i] = mSVF_BP * mix + dry * (1.0f - mix);
    }
}
