#include "IRProcessor.h"

#include <algorithm>
#include <cmath>

static constexpr float kPi = 3.14159265358979323846f;

IRProcessor::IRProcessor()
    : mAL(kChunk)
    , mAR(kChunk)
    , mBL(kChunk)
    , mBR(kChunk) {
}

void IRProcessor::onSampleRateChanged(double sr) {
    mA.setTargetSampleRate(sr);
    mB.setTargetSampleRate(sr);
    mDesignedLow = mDesignedHigh = -1.0f; // redesign at the new rate
}

void IRProcessor::reset() {
    mA.reset();
    mB.reset();
    for (int c = 0; c < 2; ++c) {
        mHp[c].clear();
        mLp[c].clear();
    }
}

void IRProcessor::designCuts(float lowHz, float highHz) {
    // RBJ Butterworth high-pass / low-pass (Q = 0.707).
    const float fs = static_cast<float>(mSampleRate);
    auto design = [fs](float f, bool high, Biquad& b) {
        f = std::clamp(f, 10.0f, 0.45f * fs);
        const float w = 2.0f * kPi * f / fs, cw = std::cos(w),
                    alpha = std::sin(w) / (2.0f * 0.7071f);
        const float a0 = 1.0f + alpha;
        const float k = high ? (1.0f + cw) / 2.0f : (1.0f - cw) / 2.0f;
        b.b0 = k / a0;
        b.b1 = (high ? -2.0f * k : 2.0f * k) / a0;
        b.b2 = k / a0;
        b.a1 = -2.0f * cw / a0;
        b.a2 = (1.0f - alpha) / a0;
    };
    for (int c = 0; c < 2; ++c) {
        design(lowHz, true, mHp[c]);
        design(highHz, false, mLp[c]);
    }
    mDesignedLow = lowHz;
    mDesignedHigh = highHz;
}

bool IRProcessor::process(float* L, float* R, int n, bool inputIsStereo) {
    // Long blocks in chunks so the scratch never grows on the audio thread.
    bool stereo = inputIsStereo;
    for (int off = 0; off < n; off += kChunk) {
        stereo = processChunk(L + off, R + off, std::min(kChunk, n - off), inputIsStereo);
    }
    return stereo;
}

bool IRProcessor::processChunk(float* L, float* R, int n, bool inputIsStereo) {
    const bool on = mEnabled.load();
    const bool hasA = mA.isLoaded();
    const bool hasB = mB.isLoaded();

    // Both loaders run every block (counting blocks for RtSlot reclaim);
    // a bypassed cab or an empty slot leaves the scratch untouched.
    std::copy(L, L + n, mAL.data());
    std::copy(R, R + n, mAR.data());
    std::copy(L, L + n, mBL.data());
    std::copy(R, R + n, mBR.data());
    mA.processBlock(mAL.data(), mAR.data(), n, inputIsStereo, on && hasA);
    mB.processBlock(mBL.data(), mBR.data(), n, inputIsStereo, on && hasB);
    if (!on || (!hasA && !hasB)) {
        return inputIsStereo;
    }

    // Per-slot gains: level × blend share × pan (constant power, unity at
    // centre) × phase. A single loaded slot plays at its own level only.
    const float blend = std::clamp(mBlend.load(), 0.0f, 100.0f) / 100.0f;
    float gL[2], gR[2];
    bool panned = false;
    for (int s = 0; s < 2; ++s) {
        const bool loaded = s == 0 ? hasA : hasB;
        // Linear crossfade: two similar cabs at 50 % sum to the same level.
        const float share = !loaded           ? 0.0f
                            : !(hasA && hasB) ? 1.0f
                                              : (s == 0 ? 1.0f - blend : blend);
        const float lvl = std::pow(10.0f, mLevelDb[s].load() / 20.0f) * share *
                          (mInvert[s].load() ? -1.0f : 1.0f);
        const float pan = std::clamp(mPan[s].load(), -100.0f, 100.0f) / 100.0f;
        const float ang = (pan + 1.0f) * 0.25f * kPi;
        gL[s] = lvl * std::cos(ang) * std::sqrt(2.0f);
        gR[s] = lvl * std::sin(ang) * std::sqrt(2.0f);
        if (loaded && std::abs(pan) > 0.001f) {
            panned = true;
        }
    }
    const bool stereo = inputIsStereo || panned;

    const float lo = mLowCutHz.load(), hi = mHighCutHz.load();
    if (lo != mDesignedLow || hi != mDesignedHigh) {
        designCuts(lo, hi);
    }
    const bool doLo = lo > 20.5f, doHi = hi < 19999.0f;

    for (int i = 0; i < n; ++i) {
        float l = gL[0] * mAL[i] + gL[1] * mBL[i];
        float r =
            gR[0] * (inputIsStereo ? mAR[i] : mAL[i]) + gR[1] * (inputIsStereo ? mBR[i] : mBL[i]);
        if (!stereo) {
            r = l;
        }
        if (doLo) {
            l = mHp[0].process(l);
            if (stereo) {
                r = mHp[1].process(r);
            }
        }
        if (doHi) {
            l = mLp[0].process(l);
            if (stereo) {
                r = mLp[1].process(r);
            }
        }
        L[i] = l;
        R[i] = stereo ? r : l;
    }
    return stereo;
}
