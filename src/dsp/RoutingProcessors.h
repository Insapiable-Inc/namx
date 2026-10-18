#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numbers>

#include "EffectBase.h"

namespace namx
{

// The graph's routing nodes besides Merge (ui/node-graph, user 2026-10-18): Crossover
// and Pan split one signal in two (STEREO kind: one in, two out), Meter passes it
// through and measures it. Not effects: no bypass (NodeType::bypass = false).

// Crossover: Low and High bands of the input, Linkwitz-Riley 4th order (two Butterworth
// biquads in series per band, RBJ cookbook). The bands are in phase with each other and
// sum to an all-pass — flat in level — so a Merge at 50 % gives back the input at
// −6 dB (Mix 50 % halves each side).
class CrossoverProcessor : public EffectBase
{
public:
    static constexpr double FREQ_MIN = 60.0;
    static constexpr double FREQ_MAX = 5000.0;
    static constexpr double FREQ_DEFAULT = 300.0;

    CrossoverProcessor() {
        set_enabled(true);
    }
    bool supports_stereo_out() const override {
        return true;
    }
    void reset() override {
        for (auto& b : low) {
            b.reset();
        }
        for (auto& b : high) {
            b.reset();
        }
    }
    void set_freq(float hz) {
        freq.store(std::clamp(static_cast<double>(hz), FREQ_MIN, FREQ_MAX),
                   std::memory_order_relaxed);
    }

protected:
    void on_sample_rate_changed(double) override {
        applied_freq = 0.0; // redesign at the new rate
        reset();
    }
    // `l` and `r` both hold the input: Low into `l`, High into `r`.
    void process_block_stereo_out(float* l, float* r, size_t n) override {
        design();
        for (size_t i = 0; i < n; ++i) {
            l[i] = low[1].process(low[0].process(l[i]));
            r[i] = high[1].process(high[0].process(r[i]));
        }
    }
    void process_block_mono(float* buf, size_t n) override {
        // Only reached outside the graph's STEREO path: the low band.
        design();
        for (size_t i = 0; i < n; ++i) {
            buf[i] = low[1].process(low[0].process(buf[i]));
        }
    }

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        float process(float in) {
            const double y = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = in;
            y2 = y1;
            y1 = y;
            return static_cast<float>(y);
        }
        void reset() {
            x1 = x2 = y1 = y2 = 0.0;
        }
    };

    // Audio thread: new coefficients when the frequency changed (the state is kept).
    void design() {
        const double f = freq.load(std::memory_order_relaxed);
        if (f == applied_freq || sample_rate <= 0.0) {
            return;
        }
        applied_freq = f;
        const double w = 2.0 * std::numbers::pi * std::min(f, 0.45 * sample_rate) / sample_rate;
        const double alpha = std::sin(w) / (2.0 * std::numbers::sqrt2 / 2.0); // Q = 1/√2
        const double c = std::cos(w);
        const double a0 = 1.0 + alpha;
        Biquad lp;
        lp.b0 = (1.0 - c) / 2.0 / a0;
        lp.b1 = (1.0 - c) / a0;
        lp.b2 = lp.b0;
        lp.a1 = -2.0 * c / a0;
        lp.a2 = (1.0 - alpha) / a0;
        Biquad hp;
        hp.b0 = (1.0 + c) / 2.0 / a0;
        hp.b1 = -(1.0 + c) / a0;
        hp.b2 = hp.b0;
        hp.a1 = lp.a1;
        hp.a2 = lp.a2;
        for (auto& b : low) {
            b.b0 = lp.b0, b.b1 = lp.b1, b.b2 = lp.b2, b.a1 = lp.a1, b.a2 = lp.a2;
        }
        for (auto& b : high) {
            b.b0 = hp.b0, b.b1 = hp.b1, b.b2 = hp.b2, b.a1 = hp.a1, b.a2 = hp.a2;
        }
    }

    atomic<double> freq{FREQ_DEFAULT};
    double applied_freq = 0.0; // audio thread
    Biquad low[2];
    Biquad high[2];
};

// Pan: the input placed between L and R, constant power (−3 dB each side in the
// centre); the gains glide across each block, so moving the knob doesn't zipper.
class PanProcessor : public EffectBase
{
public:
    PanProcessor() {
        set_enabled(true);
    }
    bool supports_stereo_out() const override {
        return true;
    }
    void reset() override {
        gain_l = target_l();
        gain_r = target_r();
    }
    // −100 (left) … 0 (centre) … +100 (right).
    void set_pan_pct(float pct) {
        pan.store(std::clamp(pct / 100.0f, -1.0f, 1.0f), std::memory_order_relaxed);
    }

protected:
    void process_block_stereo_out(float* l, float* r, size_t n) override {
        const float to_l = target_l();
        const float to_r = target_r();
        const float step_l = (to_l - gain_l) / static_cast<float>(std::max<size_t>(n, 1));
        const float step_r = (to_r - gain_r) / static_cast<float>(std::max<size_t>(n, 1));
        for (size_t i = 0; i < n; ++i) {
            gain_l += step_l;
            gain_r += step_r;
            l[i] *= gain_l;
            r[i] *= gain_r;
        }
        gain_l = to_l;
        gain_r = to_r;
    }

private:
    float angle() const {
        return (pan.load(std::memory_order_relaxed) + 1.0f) * std::numbers::pi_v<float> / 4.0f;
    }
    float target_l() const {
        return std::cos(angle());
    }
    float target_r() const {
        return std::sin(angle());
    }

    atomic<float> pan{0.0f};
    float gain_l = std::numbers::sqrt2_v<float> / 2.0f; // audio thread
    float gain_r = std::numbers::sqrt2_v<float> / 2.0f;
};

// Meter: passes the signal through untouched and keeps its peak until the GUI takes it
// (take_peak), so a poll can't miss a transient between blocks.
class MeterProcessor : public EffectBase
{
public:
    MeterProcessor() {
        set_enabled(true);
    }
    void reset() override {
    }
    // GUI thread: the peak (linear) since the last call.
    float take_peak() {
        return peak.exchange(0.0f, std::memory_order_relaxed);
    }

protected:
    void process_block_mono(float* buf, size_t n) override {
        float p = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            p = std::max(p, std::abs(buf[i]));
        }
        if (p > peak.load(std::memory_order_relaxed)) {
            peak.store(p, std::memory_order_relaxed);
        }
    }

private:
    atomic<float> peak{0.0f};
};

} // namespace namx
