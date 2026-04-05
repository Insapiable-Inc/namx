#include "VolumeSweepProcessor.h"
#include <cmath>
#include <algorithm>

void VolumeSweepProcessor::setSampleRate(double sr)
{
    mSampleRate = sr;
    // Envelope release ~80 ms
    mEnvRelease = std::exp(-1.0 / (0.080 * sr));
    reset();
}

void VolumeSweepProcessor::reset()
{
    mEnv  = 0.0f;
    mGain = 0.0f;
}

void VolumeSweepProcessor::processBlock(float* buf, int nframes)
{
    const float attackMs   = mAttack.load();
    const float sens       = mSensitivity.load() / 100.0f;
    const float mix        = mMix.load() / 100.0f;

    // Map sensitivity → trigger threshold: high sens = triggers on soft signals
    const float threshold = 0.45f * (1.0f - sens) + 0.005f * sens;

    // Per-sample gain increment for the swell
    const float gainInc = 1.0f / std::max(1.0f, attackMs * 0.001f
                                           * static_cast<float>(mSampleRate));

    for (int i = 0; i < nframes; ++i)
    {
        const float dry = buf[i];

        // Envelope follower: instant attack, slow release
        float level = std::abs(dry);
        mEnv = (level > mEnv) ? level : (mEnvRelease * mEnv);

        if (mEnv > threshold)
            mGain = std::min(1.0f, mGain + gainInc);
        else
            mGain = 0.0f; // reset on silence → next note gets a fresh swell

        buf[i] = dry * mGain * mix + dry * (1.0f - mix);
    }
}
