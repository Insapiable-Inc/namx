#pragma once

#include <algorithm>
#include <atomic>

#include "Common.h"
#include "EffectBase.h"
#include "Rt2Rotary.h"

namespace namx
{

// Rotary speaker: a model of a Boss RT-2 Rotary Ensemble (Rt2Rotary.h), its three Modes as the
// Type (`rotary/type`, append only: 1–3 = Mode I, II, III; the classic Leslie-122 model, 0, was
// removed 2026-11-07 and a preset's old 0 loads as Mode I). The knobs are the pedal's: Slow
// and Fast (the horn's speed at each, Hz on the pedal's curves; Slow 7:00 stops the rotors),
// Drive, Balance (drum ↔ horn), Level, and the Rise/Fall switch (SLOW: the rotors take their
// time to change speed). The Fast switch is the pedal's long-press (the LED red). Stereo is
// the pedal's two outputs (two microphones); mono its Output A. A stereo input is summed, like
// a speaker cabinet would.
class RotaryProcessor : public EffectBase
{
public:
    static constexpr int MODE_I = 1;
    static constexpr int MODE_II = 2;
    static constexpr int MODE_III = 3;
    static constexpr int FIRST_TYPE = MODE_I;
    static constexpr int LAST_TYPE = MODE_III;

    bool supports_stereo_out() const override {
        return true;
    }
    void reset() override;

    void set_type(int t) {
        type.store(std::clamp(t, FIRST_TYPE, LAST_TYPE));
    }
    void set_fast(bool on) {
        fast.store(on);
    }
    // Rise/Fall: true = SLOW (gentle speed changes), false = FAST.
    void set_slow_ramps(bool on) {
        slow_ramps.store(on);
    }
    // 0–100 % of each knob's rotation (50 % = 12:00). Slow and Fast set the horn's speed with
    // the Fast switch off and on, on the pedal's measured curves (rt2::SLOW_HZ, rt2::FAST_HZ).
    void set_slow_pct(float pct) {
        slow_pct.store(pct);
    }
    void set_fast_pct(float pct) {
        fast_pct.store(pct);
    }
    void set_drive_pct(float pct) {
        drive_pct.store(pct);
    }
    void set_balance_pct(float pct) {
        balance_pct.store(pct);
    }
    void set_level_pct(float pct) {
        level_pct.store(pct);
    }
    // The Drive runs oversampled: its filters' delay.
    double latency_ms() const override {
        const double rate = rate_for_any_thread.load(std::memory_order_relaxed);
        return enabled.load() && rate > 0.0 ? rt2::Engine::latency_samples() / rate * 1000.0 : 0.0;
    }

protected:
    void on_sample_rate_changed(double sr) override;
    void process_block_mono(float* buf, size_t nframes) override;
    void process_block_dual(float* l, float* r, size_t nframes) override;
    void process_block_stereo_out(float* l, float* r, size_t nframes) override;

private:
    rt2::Settings settings() const;

    atomic<int> type{MODE_I};
    atomic<bool> fast{false};
    atomic<bool> slow_ramps{false};
    atomic<float> slow_pct{50.0f};
    atomic<float> fast_pct{50.0f};
    atomic<float> drive_pct{0.0f};
    atomic<float> balance_pct{50.0f};
    atomic<float> level_pct{50.0f};

    rt2::Engine engine;
};

} // namespace namx
