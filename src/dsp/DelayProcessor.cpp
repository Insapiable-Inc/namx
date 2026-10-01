#include "DelayProcessor.h"

#include <algorithm>
#include <cmath>

static constexpr float kTwoPi = 6.28318530718f;

void DelayProcessor::onSampleRateChanged(double sr) {
    const size_t n = static_cast<size_t>(kMaxDelayMs * sr / 1000.0) + 4;
    for (auto& l : mLine) {
        l.samples.resize(n);
    }
    for (auto& s : mShift) {
        s.prepare(sr, 60.0f);
        s.setSemitones(12.0f);
    }
    reset();
}

void DelayProcessor::reset() {
    for (auto& l : mLine) {
        l.samples.clear();
        l.hpX = l.hpY = l.lp = 0.0f;
    }
    mModPhase = 0.0f;
    for (auto& s : mShift) {
        s.reset();
    }
}

float DelayProcessor::loop(int line, float d, const Params& p) {
    if (p.crystals <= 0.0f) {
        return d;
    }
    return d + p.crystals * (mShift[line].process(d) - d);
}

DelayProcessor::Params DelayProcessor::params() const {
    const float fs = static_cast<float>(mSampleRate);
    Params p;
    p.fb = std::min(mFeedback.load() / 100.0f, 0.99f);
    p.mix = mMix.load() / 100.0f;
    p.cross = std::clamp(mCrossPct.load(), 0.0f, 100.0f) / 100.0f;
    p.hpA = std::exp(-kTwoPi * std::clamp(mLowCutHz.load(), 20.0f, 2000.0f) / fs);
    p.lpK = 1.0f - std::exp(-kTwoPi * std::clamp(mHighCutHz.load(), 500.0f, 0.45f * fs) / fs);
    p.satDrive = std::clamp(mSatPct.load(), 0.0f, 100.0f) / 100.0f;
    p.modDepth = 0.003f * fs * std::clamp(mModPct.load(), 0.0f, 100.0f) / 100.0f; // up to ±3 ms
    p.phInc = 0.55f / fs; // 0.55 Hz wobble
    p.crystals = std::clamp(mCrystalsPct.load(), 0.0f, 100.0f) / 100.0f;
    const float maxD = static_cast<float>(mLine[0].samples.size()) - 2.0f - p.modDepth;
    p.delL = std::clamp(mTimeMS.load() * fs / 1000.0f, 1.0f + p.modDepth, maxD);
    p.delR = std::clamp(p.delL * std::clamp(mRightPct.load(), 25.0f, 200.0f) / 100.0f,
                        1.0f + p.modDepth, maxD);
    return p;
}

float DelayProcessor::tone(Line& l, float x, const Params& p) const {
    const float hp = p.hpA * (l.hpY + x - l.hpX);
    l.hpX = x;
    l.hpY = hp;
    l.lp += p.lpK * (hp - l.lp);
    return l.lp;
}

float DelayProcessor::saturate(float x, float amount) {
    if (amount <= 0.0f) {
        return x;
    }
    // Blend toward a soft clip (Padé tanh): unity small-signal gain, loud
    // repeats round off and compress — tape-like, never harsher.
    const float v = std::clamp(x, -3.0f, 3.0f);
    const float t = v * (27.0f + v * v) / (27.0f + 9.0f * v * v);
    return x + amount * (t - x);
}

void DelayProcessor::processBlockMono(float* buf, size_t nframes) {
    if (mLine[0].samples.empty()) {
        return;
    }
    const Params p = params();
    Line& l = mLine[0];
    for (size_t i = 0; i < nframes; ++i) {
        const float mod = p.modDepth * std::sin(kTwoPi * mModPhase);
        mModPhase += p.phInc;
        if (mModPhase >= 1.0f) {
            mModPhase -= 1.0f;
        }

        const float d = tone(l, l.read(p.delL + mod), p);
        l.push(saturate(buf[i] + p.fb * loop(0, d, p), p.satDrive));
        buf[i] = buf[i] * (1.0f - p.mix) + d * p.mix;
    }
}

void DelayProcessor::processBlockDual(float* L, float* R, size_t nframes) {
    // Stereo input: independent line per channel (no cross-feedback).
    if (mLine[0].samples.empty()) {
        return;
    }
    const Params p = params();
    for (size_t i = 0; i < nframes; ++i) {
        const float mod = p.modDepth * std::sin(kTwoPi * mModPhase);
        mModPhase += p.phInc;
        if (mModPhase >= 1.0f) {
            mModPhase -= 1.0f;
        }

        const float dL = tone(mLine[0], mLine[0].read(p.delL + mod), p);
        const float dR = tone(mLine[1], mLine[1].read(p.delR - mod), p);
        mLine[0].push(saturate(L[i] + p.fb * loop(0, dL, p), p.satDrive));
        mLine[1].push(saturate(R[i] + p.fb * loop(1, dR, p), p.satDrive));
        L[i] = L[i] * (1.0f - p.mix) + dL * p.mix;
        R[i] = R[i] * (1.0f - p.mix) + dR * p.mix;
    }
}

void DelayProcessor::processBlockStereoOut(float* L, float* R, size_t nframes) {
    // Cross-feed c: 1 = ping-pong (input → left only, repeats alternate),
    // 0 = two independent lines (input into both, L/R times).
    if (mLine[0].samples.empty()) {
        return;
    }
    const Params p = params();
    const float c = p.cross;
    for (size_t i = 0; i < nframes; ++i) {
        const float mod = p.modDepth * std::sin(kTwoPi * mModPhase);
        mModPhase += p.phInc;
        if (mModPhase >= 1.0f) {
            mModPhase -= 1.0f;
        }

        const float x = L[i];
        const float dL = tone(mLine[0], mLine[0].read(p.delL + mod), p);
        const float dR = tone(mLine[1], mLine[1].read(p.delR - mod), p);
        const float fL = loop(0, dL, p), fR = loop(1, dR, p);
        mLine[0].push(saturate(x + p.fb * ((1.0f - c) * fL + c * fR), p.satDrive));
        mLine[1].push(saturate((1.0f - c) * x + p.fb * ((1.0f - c) * fR + c * fL), p.satDrive));
        L[i] = x * (1.0f - p.mix) + dL * p.mix;
        R[i] = R[i] * (1.0f - p.mix) + dR * p.mix;
    }
}
