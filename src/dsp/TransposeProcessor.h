#pragma once

#include <atomic>

#include "EffectBase.h"
#include "PitchShifter.h"

/// Transpose ±12 semitones (drop tunings, octave effects) at the front of
/// the chain. Mix blends the dry signal back in (100 % = transposed only).
/// At 0 semitones it passes the signal straight through (no added latency).
class TransposeProcessor : public EffectBase
{
public:
    void reset() override {
        mShift[0].reset();
        mShift[1].reset();
    }

    void setSemitones(float st) {
        mSemis.store(st);
    }
    void setMix(float pct) {
        mMix.store(pct);
    }

protected:
    void onSampleRateChanged(double sr) override {
        mShift[0].prepare(sr);
        mShift[1].prepare(sr);
    }
    void processBlockMono(float* buf, size_t n) override {
        processChannel(buf, n, 0);
    }
    void processBlockDual(float* L, float* R, size_t n) override {
        processChannel(L, n, 0);
        processChannel(R, n, 1);
    }

private:
    void processChannel(float* buf, size_t n, int ch) {
        const float st = mSemis.load(std::memory_order_relaxed);
        if (std::abs(st) < 0.01f) {
            return;
        }
        const float mix = mMix.load(std::memory_order_relaxed) / 100.0f;
        PitchShifter& s = mShift[ch];
        s.setSemitones(st);
        for (size_t i = 0; i < n; ++i) {
            buf[i] = buf[i] * (1.0f - mix) + s.process(buf[i]) * mix;
        }
    }

    std::atomic<float> mSemis{0.0f};
    std::atomic<float> mMix{100.0f};
    PitchShifter mShift[2];
};
