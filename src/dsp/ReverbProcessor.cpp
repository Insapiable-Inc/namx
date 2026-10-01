#include "ReverbProcessor.h"

#include <algorithm>
#include <cmath>

constexpr int ReverbProcessor::kCombLengths[kNumCombs];
constexpr int ReverbProcessor::kApLengths[kNumAllPasses];

ReverbProcessor::ReverbProcessor() {
    allocate();
}

void ReverbProcessor::allocate() {
    double scale = mSampleRate / 44100.0;
    for (auto& p : mPre) {
        p.resize(static_cast<size_t>(mSampleRate * kMaxPreDelayMs / 1000.0) + 2);
    }
    for (int i = 0; i < kNumCombs; ++i) {
        mCombL[i].resize(static_cast<size_t>(kCombLengths[i] * scale));
        mCombR[i].resize(static_cast<size_t>((kCombLengths[i] + kStereoOffset) * scale));
    }
    for (int i = 0; i < kNumAllPasses; ++i) {
        mApL[i].resize(static_cast<size_t>(kApLengths[i] * scale));
        mApR[i].resize(static_cast<size_t>((kApLengths[i] + kStereoOffset) * scale));
    }
}

void ReverbProcessor::onSampleRateChanged(double sr) {
    for (auto& s : mShift) {
        s.prepare(sr, 80.0f);
        s.setSemitones(12.0f);
    }
    allocate();
    reset();
}

void ReverbProcessor::reset() {
    for (auto& c : mCombL) {
        c.reset();
    }
    for (auto& c : mCombR) {
        c.reset();
    }
    for (auto& a : mApL) {
        a.reset();
    }
    for (auto& a : mApR) {
        a.reset();
    }
    for (int b = 0; b < 2; ++b) {
        mPre[b].clear();
        mHpX[b] = mHpY[b] = mLp[b] = 0.0f;
        mShift[b].reset();
        mShimFb[b] = 0.0f;
    }
}

float ReverbProcessor::preDelay(int bank, float x, int d) {
    DelayLine& line = mPre[bank];
    if (d <= 0 || line.empty()) {
        return x;
    }
    line.push(x);
    return line.at(static_cast<size_t>(d));
}

float ReverbProcessor::wetFilter(int ch, float x, float hpA, float lpK) {
    // One-pole high-pass then low-pass — gentle 6 dB/oct cuts on the tail.
    const float hp = hpA * (mHpY[ch] + x - mHpX[ch]);
    mHpX[ch] = x;
    mHpY[ch] = hp;
    mLp[ch] += lpK * (hp - mLp[ch]);
    return mLp[ch];
}

// Shared per-block parameters.
namespace
{
struct Cuts
{
    float hpA, lpK;
    int pre;
};
Cuts cutsFor(double sr, float lowHz, float highHz, float preMs, float maxPreMs) {
    constexpr float kTwoPi = 6.28318530718f;
    const float fs = static_cast<float>(sr);
    Cuts c;
    c.hpA = std::exp(-kTwoPi * std::clamp(lowHz, 20.0f, 2000.0f) / fs);
    c.lpK = 1.0f - std::exp(-kTwoPi * std::clamp(highHz, 500.0f, 0.45f * fs) / fs);
    c.pre = static_cast<int>(std::clamp(preMs, 0.0f, maxPreMs) * fs / 1000.0f);
    return c;
}
} // namespace

void ReverbProcessor::processBlockStereoOut(float* L, float* R, int nframes) {
    const float roomSize = mRoomSize.load() / 100.0f;
    const float damping = mDamping.load() / 100.0f;
    const float width = mWidth.load() / 100.0f;
    const float mix = mMix.load() / 100.0f;
    const float feedback = 0.28f + roomSize * 0.7f;
    const float damp = damping * 0.4f;
    const Cuts cut = cutsFor(mSampleRate, mLowCutHz.load(), mHighCutHz.load(), mPreDelayMs.load(),
                             kMaxPreDelayMs);

    // Shimmer loop gain stays well under 1 — the comb feedback adds its own.
    const float shim = std::clamp(mShimmer.load(), 0.0f, 100.0f) / 100.0f * 0.45f;

    for (int i = 0; i < nframes; ++i) {
        float inMono = preDelay(0, (L[i] + R[i]) * 0.015f, cut.pre) + shim * mShimFb[0];

        float outL = 0.0f, outR = 0.0f;
        for (int c = 0; c < kNumCombs; ++c) {
            outL += mCombL[c].process(inMono, feedback, damp);
            outR += mCombR[c].process(inMono, feedback, damp);
        }
        for (int a = 0; a < kNumAllPasses; ++a) {
            outL = mApL[a].process(outL);
            outR = mApR[a].process(outR);
        }

        outL = wetFilter(0, outL, cut.hpA, cut.lpK);
        outR = wetFilter(1, outR, cut.hpA, cut.lpK);
        if (shim > 0.0f) {
            mShimFb[0] = mShift[0].process(0.5f * (outL + outR) * 0.015f);
        }

        float wet1 = width * 0.5f + 0.5f;
        float wet2 = (1.0f - width) * 0.5f;
        L[i] = L[i] * (1.0f - mix) + (outL * wet1 + outR * wet2) * mix;
        R[i] = R[i] * (1.0f - mix) + (outR * wet1 + outL * wet2) * mix;
    }
}

void ReverbProcessor::processMonoBank(float* buf, int nframes, int bank,
                                      std::array<CombFilter, kNumCombs>& combs,
                                      std::array<AllPassFilter, kNumAllPasses>& aps) {
    const float roomSize = mRoomSize.load() / 100.0f;
    const float damping = mDamping.load() / 100.0f;
    const float mix = mMix.load() / 100.0f;
    const float feedback = 0.28f + roomSize * 0.7f;
    const float damp = damping * 0.4f;
    const Cuts cut = cutsFor(mSampleRate, mLowCutHz.load(), mHighCutHz.load(), mPreDelayMs.load(),
                             kMaxPreDelayMs);

    const float shim = std::clamp(mShimmer.load(), 0.0f, 100.0f) / 100.0f * 0.45f;

    for (int i = 0; i < nframes; ++i) {
        float inSig = preDelay(bank, buf[i] * 0.015f, cut.pre) + shim * mShimFb[bank];
        float out = 0.0f;
        for (int c = 0; c < kNumCombs; ++c) {
            out += combs[c].process(inSig, feedback, damp);
        }
        for (int a = 0; a < kNumAllPasses; ++a) {
            out = aps[a].process(out);
        }
        out = wetFilter(bank, out, cut.hpA, cut.lpK);
        if (shim > 0.0f) {
            mShimFb[bank] = mShift[bank].process(out * 0.015f);
        }
        buf[i] = buf[i] * (1.0f - mix) + out * mix;
    }
}

void ReverbProcessor::processBlockMono(float* buf, int nframes) {
    processMonoBank(buf, nframes, 0, mCombL, mApL);
}

void ReverbProcessor::processBlockDual(float* L, float* R, int nframes) {
    // Stereo input: independent mono reverb per channel, each on its own
    // comb/all-pass bank so the tails don't share state.
    processMonoBank(L, nframes, 0, mCombL, mApL);
    processMonoBank(R, nframes, 1, mCombR, mApR);
}
