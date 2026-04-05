#include "OctaveProcessor.h"
#include <cmath>

void OctaveProcessor::setSampleRate(double sr)
{
    mSampleRate = sr;
    // Release time ~50 ms
    mEnvRelease = std::exp(-1.0 / (0.050 * sr));
    reset();
}

void OctaveProcessor::reset()
{
    mPrevRect   = 0.0f;
    mDCState    = 0.0f;
    mFlipFlop   = 1.0f;
    mPrevSample = 0.0f;
    mEnvelope   = 0.0f;
}

void OctaveProcessor::processBlock(float* buf, int nframes)
{
    const float upLevel   = mUpLevel.load()   / 100.0f;
    const float downLevel = mDownLevel.load() / 100.0f;

    const bool doUp   = upLevel   > 1e-4f;
    const bool doDown = downLevel > 1e-4f;
    if (!doUp && !doDown) return;

    for (int i = 0; i < nframes; ++i)
    {
        const float dry = buf[i];
        float out = dry;

        // ---- Octave up ----
        if (doUp)
        {
            // Full-wave rectify: fundamental → 2× fundamental
            float rect = std::abs(dry);
            // DC blocker: y[n] = x[n] - x[n-1] + 0.995 * y[n-1]
            float upSig = rect - mPrevRect + 0.995f * mDCState;
            mPrevRect = rect;
            mDCState  = upSig;
            out += upSig * upLevel;
        }

        // ---- Octave down ----
        if (doDown)
        {
            // Envelope follower: fast attack, slow release
            float env = std::abs(dry);
            mEnvelope = (env > mEnvelope) ? env : (mEnvRelease * mEnvelope);

            // Zero-crossing flip-flop
            if ((dry >= 0.0f) != (mPrevSample >= 0.0f))
                mFlipFlop = -mFlipFlop;

            out += mFlipFlop * mEnvelope * downLevel;
        }

        mPrevSample = dry;
        buf[i] = out;
    }
}
