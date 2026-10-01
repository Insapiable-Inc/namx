#pragma once

#include <atomic>

#include "EffectBase.h"

/// Overdrive / boost in front of the amp, Tube-Screamer style: only the
/// high-passed part of the signal (above ~720 Hz) goes through the gain +
/// soft-clip stage and is added back to the clean signal — the mid hump and
/// tight lows every high-gain rig wants — then a tone low-pass and level.
class DriveProcessor : public EffectBase
{
public:
    void reset() override;

    void set_drive_pct(float pct) {
        m_drive_pct.store(pct);
    } // 0–100
    void set_tone_pct(float pct) {
        m_tone_pct.store(pct);
    } // 0–100: dark → bright
    void set_level(float db) {
        m_level_db.store(db);
    }

protected:
    void on_sample_rate_changed(double) override {
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

    std::atomic<float> m_drive_pct{30.0f};
    std::atomic<float> m_tone_pct{50.0f};
    std::atomic<float> m_level_db{0.0f};

    // Per-channel state: input high-pass, tone low-pass.
    float m_hp_x1[2]{}, m_hp_y1[2]{}, m_lp_per_channel[2]{};
};
