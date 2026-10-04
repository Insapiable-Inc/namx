#pragma once

#include <atomic>

#include "Common.h"
#include "Constants.h"
#include "EffectBase.h"
#include "TubeScreamerClipper.h"

namespace namx
{

// Overdrive / boost in front of the amp: the Tube Screamer's clipping stage modelled as a
// circuit (TubeScreamerClipper — only what's above ~720 Hz gets the gain and the diodes,
// the clean signal is added back: the mid hump and tight lows every high-gain rig wants),
// then a tone low-pass and level.
class DriveProcessor : public EffectBase
{
public:
    void reset() override;
    void sync_right_channel() override;

    void set_drive_pct(float pct) {
        drive_pct.store(pct);
    } // 0–100
    void set_tone_pct(float pct) {
        tone_pct.store(pct);
    } // 0–100: dark → bright
    void set_level(float db) {
        level_db.store(db);
    }
    double latency_ms() const override {
        const double rate = rate_for_any_thread.load(std::memory_order_relaxed);
        return enabled.load() && rate > 0.0 ? TubeScreamerClipper::latency_samples() / rate * 1000.0
                                            : 0.0;
    }

protected:
    void on_sample_rate_changed(double sr) override {
        circuit[0].prepare(sr);
        circuit[1].prepare(sr);
        reset();
    }
    void process_block_mono(float* buf, size_t n) override {
        process_channel(buf, n, 0);
    }
    void process_block_dual(float* l, float* r, size_t n) override {
        process_channel(l, n, 0);
        process_channel(r, n, 1);
    }

private:
    void process_channel(float* buf, size_t n, int ch);

    atomic<float> drive_pct{static_cast<float>(NAMConstants::DRIVE_DEFAULT)};
    atomic<float> tone_pct{static_cast<float>(NAMConstants::DRIVE_TONE_DEFAULT)};
    atomic<float> level_db{static_cast<float>(NAMConstants::DRIVE_LEVEL_DEFAULT)};

    static constexpr float CIRCUIT_VOLTS = 1.0f; // digital full scale at the pedal's input
    static constexpr float MAKEUP = 0.255f; // output level at Level 0 dB

    // Per-channel state: the circuit, the tone low-pass.
    TubeScreamerClipper circuit[2];
    float lp_per_channel[2]{};
};

} // namespace namx
