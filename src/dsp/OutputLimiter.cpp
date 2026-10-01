#include "OutputLimiter.h"

#include <algorithm>
#include <cmath>

static constexpr float kCeiling = 0.96605f; // −0.3 dBFS

void OutputLimiter::setSampleRate(double sr) {
    mLook = std::max(1, static_cast<int>(sr * 0.001));
    mDelayL.assign(mLook, 0.0f);
    mDelayR.assign(mLook, 0.0f);
    mNeed.assign(mLook, 1.0f);
    mPos = 0;
    mGain = 1.0f;
    mRelease = std::exp(-1.0f / (0.080f * static_cast<float>(sr)));
    // Reach the target well within the look-ahead (5 time constants).
    mAttack = 1.0f - std::exp(-5.0f / mLook);
}

void OutputLimiter::process(float* L, float* R, size_t n) {
    const bool on = mEnabled.load(std::memory_order_relaxed);
    if (!on) {
        if (mWasEnabled) // flush the look-ahead so re-enabling doesn't replay old audio
        {
            std::fill(mDelayL.begin(), mDelayL.end(), 0.0f);
            std::fill(mDelayR.begin(), mDelayR.end(), 0.0f);
            std::fill(mNeed.begin(), mNeed.end(), 1.0f);
            mGain = 1.0f;
        }
        mWasEnabled = false;
        return;
    }
    mWasEnabled = true;
    if (mDelayL.empty()) {
        return;
    }

    float minGain = 1.0f;
    for (size_t i = 0; i < n; ++i) {
        const float peak = std::max(std::abs(L[i]), std::abs(R[i]));
        const float need = peak > kCeiling ? kCeiling / peak : 1.0f;

        // Write the new sample into the look-ahead; the oldest comes out.
        const float outL = mDelayL[mPos], outR = mDelayR[mPos];
        mDelayL[mPos] = L[i];
        mDelayR[mPos] = R[i];
        mNeed[mPos] = need;
        if (++mPos == mLook) {
            mPos = 0;
        }

        // Lowest gain any sample in the window (the next 1 ms) will need.
        float target = 1.0f;
        for (float g : mNeed) {
            target = std::min(target, g);
        }

        mGain = target < mGain ? mGain + mAttack * (target - mGain)
                               : target + mRelease * (mGain - target);
        // Safety: never let a sample out above the ceiling.
        const float g =
            std::min(mGain, kCeiling / std::max({std::abs(outL), std::abs(outR), kCeiling}));
        L[i] = outL * g;
        R[i] = outR * g;
        minGain = std::min(minGain, g);
    }
    const float grDb = -20.0f * std::log10(std::max(minGain, 1e-4f));
    if (grDb > mGrPeakDb.load(std::memory_order_relaxed)) {
        mGrPeakDb.store(grDb, std::memory_order_relaxed);
    }
}
