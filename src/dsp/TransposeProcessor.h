#pragma once

#include <atomic>
#include <cmath>

#include "Common.h"
#include "EffectBase.h"
#include "PitchShifter.h"

namespace namx
{

// Transpose ±12 semitones (drop tunings, octave effects) at the front of
// the chain. Mix blends the dry signal back in (100 % = transposed only).
// At 0 semitones it passes the signal straight through (no added latency) —
// except in continuous mode (whammy sweeps), where the shifter keeps running so
// a pedal passing through 0 doesn't jump between the dry and the delayed path.
class TransposeProcessor : public EffectBase
{
public:
    void reset() override {
        shift[0].reset();
        shift[1].reset();
    }

    void set_semitones(float st) {
        semis.store(st);
    }
    void set_continuous(bool on) {
        continuous.store(on);
    }
    // Shifting (or ready to, in continuous mode): about half the shifter's window.
    double latency_ms() const override {
        const bool shifting = std::abs(semis.load()) >= 0.01f || continuous.load();
        return enabled.load() && shifting && sample_rate > 0.0
                   ? shift[0].latency_samples() / sample_rate * 1000.0
                   : 0.0;
    }

    void set_mix_pct(float pct) {
        mix_pct.store(pct);
    }

protected:
    void on_sample_rate_changed(double sr) override {
        shift[0].prepare(sr);
        shift[1].prepare(sr);
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
        const float st = semis.load(std::memory_order_relaxed);
        if (std::abs(st) < 0.01f && !continuous.load(std::memory_order_relaxed)) {
            return;
        }
        const float mix = mix_pct.load(std::memory_order_relaxed) / 100.0f;
        PitchShifter& s = shift[ch];
        s.set_semitones(st);
        for (size_t i = 0; i < n; ++i) {
            buf[i] = buf[i] * (1.0f - mix) + s.process(buf[i]) * mix;
        }
    }

    atomic<float> semis{0.0f};
    atomic<float> mix_pct{100.0f};
    atomic<bool> continuous{false};
    PitchShifter shift[2];
};

} // namespace namx
