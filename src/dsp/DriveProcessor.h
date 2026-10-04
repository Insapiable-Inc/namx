#pragma once

#include <atomic>

#include "Common.h"
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
        return enabled.load() && sample_rate > 0.0
                   ? TubeScreamerClipper::latency_samples() / sample_rate * 1000.0
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

    atomic<float> drive_pct{30.0f};
    atomic<float> tone_pct{50.0f};
    atomic<float> level_db{0.0f};

    static constexpr float CIRCUIT_VOLTS = 1.0f; // digital full scale at the pedal's input
    static constexpr float MAKEUP = 0.255f; // output level at Level 0 dB

    // Per-channel state: the circuit, the tone low-pass.
    TubeScreamerClipper circuit[2];
    float lp_per_channel[2]{};
};

} // namespace namx
