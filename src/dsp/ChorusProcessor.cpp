#include "ChorusProcessor.h"

#include <algorithm>
#include <cmath>

static constexpr float kPi = 3.14159265358979323846f;

void ChorusProcessor::onSampleRateChanged(double sr) {
    const size_t length = static_cast<size_t>((kBaseMs + kMaxDepthMs + 5.0f) * sr / 1000.0f) + 4;
    mLineL.resize(length);
    mLineR.resize(length);
    reset();
}

void ChorusProcessor::reset() {
    mLineL.clear();
    mLineR.clear();
    mPhase = 0.0f;
    mVintLp[0] = mVintLp[1] = 0.0f;
}

float ChorusProcessor::wet(const DelayLine& line, float phase, float depth, float fs, int mode) {
    auto tap = [&](float ph) {
        float lfo;
        if (mode == Vintage) {
            const float f = ph - std::floor(ph);
            lfo = 4.0f * std::abs(f - 0.5f) - 1.0f; // triangle
        } else {
            lfo = std::sin(2.0f * kPi * ph);
        }
        const float del = std::max(1.0f, (kBaseMs + depth * kMaxDepthMs * lfo) * fs / 1000.0f);
        return line.interpolate(del);
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

    for (int i = 0; i < nframes; ++i) {
        mLineL.push(buf[i]);
        float w = wet(mLineL, mPhase, depth, fs, mode);
        if (mode == Vintage) {
            w = vintageTone(w, mVintLp[0], lpK);
        }
        buf[i] = buf[i] * (1.0f - mix) + w * mix;
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

    for (int i = 0; i < nframes; ++i) {
        // Same LFO for both channels: identical mono effect applied per channel.
        mLineL.push(L[i]);
        mLineR.push(R[i]);
        float wL = wet(mLineL, mPhase, depth, fs, mode);
        float wR = wet(mLineR, mPhase, depth, fs, mode);
        if (mode == Vintage) {
            wL = vintageTone(wL, mVintLp[0], lpK);
            wR = vintageTone(wR, mVintLp[1], lpK);
        }
        L[i] = L[i] * (1.0f - mix) + wL * mix;
        R[i] = R[i] * (1.0f - mix) + wR * mix;
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

    for (int i = 0; i < nframes; ++i) {
        mLineL.push(L[i]);
        mLineR.push(R[i]);
        float wL = wet(mLineL, mPhase, depth, fs, mode);
        float wR = wet(mLineR, mPhase + off, depth, fs, mode);
        if (mode == Vintage) {
            wL = vintageTone(wL, mVintLp[0], lpK);
            wR = vintageTone(wR, mVintLp[1], lpK);
        }
        L[i] = L[i] * (1.0f - mix) + wL * mix;
        R[i] = R[i] * (1.0f - mix) + rSign * wR * mix;
        mPhase += phInc;
        if (mPhase >= 1.0f) {
            mPhase -= 1.0f;
        }
    }
}
