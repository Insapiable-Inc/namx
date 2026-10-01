#include "DoublerProcessor.h"

#include <algorithm>
#include <cmath>

void DoublerProcessor::onSampleRateChanged(double sr) {
    mLine.resize(static_cast<size_t>(sr * 0.032) + 4); // 20 ms + drift headroom
    reset();
}

void DoublerProcessor::reset() {
    mLine.clear();
    mDrift = mDriftTarget = 0.0f;
    mUntilNewTarget = 0;
}

bool DoublerProcessor::process(float* L, float* R, int n, bool inputIsStereo) {
    if (!mEnabled.load() || inputIsStereo || mLine.empty()) {
        return inputIsStereo;
    }

    const float fs = static_cast<float>(mSampleRate);
    const float spread = std::clamp(mSpreadMs.load(), 3.0f, 20.0f) * 0.001f * fs;
    const float mix = std::clamp(mMix.load(), 0.0f, 100.0f) / 100.0f;
    // Drift: a new random target every ~120 ms, approached smoothly — the
    // delay wanders ±30 % of the spread, like a second player's timing.
    const int hold = static_cast<int>(0.12f * fs);
    const float glide = 1.0f - std::exp(-1.0f / (0.25f * fs));

    for (int i = 0; i < n; ++i) {
        if (--mUntilNewTarget <= 0) {
            mSeed = mSeed * 1664525u + 1013904223u;
            mDriftTarget = static_cast<float>(mSeed >> 8) / 8388608.0f - 1.0f;
            mUntilNewTarget = hold;
        }
        mDrift += glide * (mDriftTarget - mDrift);

        const float x = L[i];
        mLine.push(x);
        const float dbl = mLine.interpolate(spread * (1.0f + 0.3f * mDrift));

        L[i] = x;
        R[i] = x * (1.0f - mix) + dbl * mix;
    }
    return true;
}
