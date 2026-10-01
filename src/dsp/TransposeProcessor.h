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
        m_shift[0].reset();
        m_shift[1].reset();
    }

    void set_semitones(float st) {
        m_semis.store(st);
    }
    void set_mix_pct(float pct) {
        m_mix_pct.store(pct);
    }

protected:
    void on_sample_rate_changed(double sr) override {
        m_shift[0].prepare(sr);
        m_shift[1].prepare(sr);
    }
    void process_block_mono(float* buf, size_t n) override {
        process_channel(buf, n, 0);
    }
    void process_block_dual(float* l, float* r, size_t n) override {
        process_channel(l, n, 0);
        process_channel(r, n, 1);
    }

private:
    void process_channel(float* buf, size_t n, int ch) {
        const float st = m_semis.load(std::memory_order_relaxed);
        if (std::abs(st) < 0.01f) {
            return;
        }
        const float mix = m_mix_pct.load(std::memory_order_relaxed) / 100.0f;
        PitchShifter& s = m_shift[ch];
        s.set_semitones(st);
        for (size_t i = 0; i < n; ++i) {
            buf[i] = buf[i] * (1.0f - mix) + s.process(buf[i]) * mix;
        }
    }

    std::atomic<float> m_semis{0.0f};
    std::atomic<float> m_mix_pct{100.0f};
    PitchShifter m_shift[2];
};
