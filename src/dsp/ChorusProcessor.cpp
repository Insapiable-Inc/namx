#include "ChorusProcessor.h"

#include <algorithm>
#include <cmath>

static constexpr float kPi = 3.14159265358979323846f;

void ChorusProcessor::onSampleRateChanged(double sr) {
    int bufSamples = static_cast<int>((kBaseMs + kMaxDepthMs + 5.0f) * sr / 1000.0f) + 4;
    mBufL.assign(bufSamples, 0.0f);
    mBufR.assign(bufSamples, 0.0f);
    reset();
}

void ChorusProcessor::reset() {
    std::fill(mBufL.begin(), mBufL.end(), 0.0f);
    std::fill(mBufR.begin(), mBufR.end(), 0.0f);
    mWriteL = mWriteR = 0;
    mPhase = 0.0f;
    mVintLp[0] = mVintLp[1] = 0.0f;
}

float ChorusProcessor::readInterp(const std::vector<float>& buf, int writePos,
                                  float delaySamples) const {
    int sz = static_cast<int>(buf.size());
    float readF = static_cast<float>(writePos) - delaySamples;
    while (readF < 0.0f) {
        readF += sz;
    }

    int r0 = static_cast<int>(readF); // < sz: delaySamples >= 1
    if (r0 >= sz) {
        r0 -= sz;
    }
    int r1 = r0 + 1 == sz ? 0 : r0 + 1;
    float frac = readF - std::floor(readF);
    return buf[r0] + (buf[r1] - buf[r0]) * frac;
}

float ChorusProcessor::wet(const std::vector<float>& buf, int write, float phase, float depth,
                           float fs, int mode) {
    auto tap = [&](float ph) {
        float lfo;
        if (mode == Vintage) {
            const float f = ph - std::floor(ph);
            lfo = 4.0f * std::abs(f - 0.5f) - 1.0f; // triangle
        } else {
            lfo = std::sin(2.0f * kPi * ph);
        }
        const float del = std::max(1.0f, (kBaseMs + depth * kMaxDepthMs * lfo) * fs / 1000.0f);
        return readInterp(buf, write, del);
    };
    if (mode == Tri) {
        return (tap(phase) + tap(phase + 1.0f / 3.0f) + tap(phase + 2.0f / 3.0f)) * (1.0f / 3.0f);
    }
    return tap(phase);
}

// One processing loop for every dispatch case: `stereoOut` = mono in, true
// stereo out (L/R LFOs offset); `dual` = stereo in, same LFO per channel.
static inline float vintageTone(float x, float& lp, float k) {
    lp += k * (x - lp);
    return lp;
}

void ChorusProcessor::processBlockMono(float* buf, int nframes) {
    const float depth = mDepth.load() / 100.0f;
    const float mix = mMix.load() / 100.0f;
    const float fs = static_cast<float>(mSampleRate);
    const float phInc = mRate.load() / fs;
    const int mode = mMode.load(std::memory_order_relaxed);
    const float lpK = 1.0f - std::exp(-2.0f * kPi * 3200.0f / fs);
    const int sz = static_cast<int>(mBufL.size());

    for (int i = 0; i < nframes; ++i) {
        mBufL[mWriteL] = buf[i];
        float w = wet(mBufL, mWriteL, mPhase, depth, fs, mode);
        if (mode == Vintage) {
            w = vintageTone(w, mVintLp[0], lpK);
        }
        buf[i] = buf[i] * (1.0f - mix) + w * mix;
        if (++mWriteL == sz) {
            mWriteL = 0;
        }
        mPhase += phInc;
        if (mPhase >= 1.0f) {
            mPhase -= 1.0f;
        }
    }
}

void ChorusProcessor::processBlockDual(float* L, float* R, int nframes) {
    const float depth = mDepth.load() / 100.0f;
    const float mix = mMix.load() / 100.0f;
    const float fs = static_cast<float>(mSampleRate);
    const float phInc = mRate.load() / fs;
    const int mode = mMode.load(std::memory_order_relaxed);
    const float lpK = 1.0f - std::exp(-2.0f * kPi * 3200.0f / fs);
    const int szL = static_cast<int>(mBufL.size());
    const int szR = static_cast<int>(mBufR.size());

    for (int i = 0; i < nframes; ++i) {
        // Same LFO for both channels: identical mono effect applied per channel.
        mBufL[mWriteL] = L[i];
        mBufR[mWriteR] = R[i];
        float wL = wet(mBufL, mWriteL, mPhase, depth, fs, mode);
        float wR = wet(mBufR, mWriteR, mPhase, depth, fs, mode);
        if (mode == Vintage) {
            wL = vintageTone(wL, mVintLp[0], lpK);
            wR = vintageTone(wR, mVintLp[1], lpK);
        }
        L[i] = L[i] * (1.0f - mix) + wL * mix;
        R[i] = R[i] * (1.0f - mix) + wR * mix;
        if (++mWriteL == szL) {
            mWriteL = 0;
        }
        if (++mWriteR == szR) {
            mWriteR = 0;
        }
        mPhase += phInc;
        if (mPhase >= 1.0f) {
            mPhase -= 1.0f;
        }
    }
}

void ChorusProcessor::processBlockStereoOut(float* L, float* R, int nframes) {
    const float depth = mDepth.load() / 100.0f;
    const float mix = mMix.load() / 100.0f;
    const float fs = static_cast<float>(mSampleRate);
    const float phInc = mRate.load() / fs;
    const int mode = mMode.load(std::memory_order_relaxed);
    const float lpK = 1.0f - std::exp(-2.0f * kPi * 3200.0f / fs);
    const float off = mode == Spatial ? 0.25f : 0.5f; // LFO offset
    const float rSign = mode == Spatial ? -1.0f : 1.0f; // Spatial: inverted right wet
    const int szL = static_cast<int>(mBufL.size());
    const int szR = static_cast<int>(mBufR.size());

    for (int i = 0; i < nframes; ++i) {
        mBufL[mWriteL] = L[i];
        mBufR[mWriteR] = R[i];
        float wL = wet(mBufL, mWriteL, mPhase, depth, fs, mode);
        float wR = wet(mBufR, mWriteR, mPhase + off, depth, fs, mode);
        if (mode == Vintage) {
            wL = vintageTone(wL, mVintLp[0], lpK);
            wR = vintageTone(wR, mVintLp[1], lpK);
        }
        L[i] = L[i] * (1.0f - mix) + wL * mix;
        R[i] = R[i] * (1.0f - mix) + rSign * wR * mix;
        if (++mWriteL == szL) {
            mWriteL = 0;
        }
        if (++mWriteR == szR) {
            mWriteR = 0;
        }
        mPhase += phInc;
        if (mPhase >= 1.0f) {
            mPhase -= 1.0f;
        }
    }
}
