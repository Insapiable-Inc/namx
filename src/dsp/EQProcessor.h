#pragma once

#include <array>
#include <atomic>
#include <cmath>

#include "Common.h"
#include "EffectBase.h"
#include "Ge7Eq.h"

namespace namx
{

// 4-band semi-parametric EQ + low/high cut (Audio EQ Cookbook biquads):
//   Bass     low shelf    40–500 Hz   (default 250 — the old fixed band)
//   Middle   peak, Q 0.7  200–2.5 kHz (default 1 kHz)
//   High-Mid peak, Q 1.0  800–8 kHz   (default 3 kHz)
//   Treble   high shelf   1.5–12 kHz  (default 4 kHz)
//   Low Cut  2nd-order high-pass 20–500 Hz (20 = off)
//   High Cut 2nd-order low-pass 2–20 kHz (20 kHz = off)
// Lock-free: the GUI only stores gains/frequencies in atomics and bumps a version;
// the audio thread owns every filter (coefficients and state) and recomputes the
// coefficients at the top of the next block — no mutex, no skipped blocks, no shared
// filter state (code review items 04, 18). Independent L/R state.
//
// Type picks the engine: Parametric (the above, the default) or GE-7, a model of a Boss GE-7
// Equalizer (Ge7Eq.h): its seven sliders and Level on the pedal's printed scale (−15…+15)
// in place of the four bands; Low Cut and High Cut stay, after it.
class EQProcessor : public EffectBase
{
public:
    // Type values (preset key eq/type): append only.
    enum Type : int
    {
        PARAMETRIC = 0,
        GE7 = 1,
    };
    static constexpr int TYPE_COUNT = 2;

    enum class Band : size_t
    {
        LOW_CUT = 0,
        BASS,
        MIDDLE,
        HIGH_MID,
        TREBLE,
        HIGH_CUT
    };
    static constexpr size_t BANDS = 6;

    EQProcessor();

    bool process(float* l, float* r, size_t n, bool input_is_stereo) override;
    void reset() override;
    void sync_right_channel() override;

    void set_gain(Band b, double db);
    void set_freq(Band b, double hz);
    void set_type(int new_type);
    int get_type() const {
        return type.load();
    }
    // GE-7 sliders: 0–6 the bands (100 Hz … 6.4 kHz), 7 Level; the printed scale.
    void set_ge7_slider(size_t slider, double mark);

    // Back-compat names (3-band era).
    void set_low_gain(double db) {
        set_gain(Band::BASS, db);
    }
    void set_mid_gain(double db) {
        set_gain(Band::MIDDLE, db);
    }
    void set_treble_gain(double db) {
        set_gain(Band::TREBLE, db);
    }

protected:
    void on_sample_rate_changed(double sr) override;

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

        float process(float in) {
            double x0 = in;
            double y0 = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = x0;
            y2 = y1;
            y1 = y0;
            return static_cast<float>(y0);
        }
        void reset() {
            x1 = x2 = y1 = y2 = 0.0;
        }
        void set_coeffs(const Biquad& s) {
            b0 = s.b0;
            b1 = s.b1;
            b2 = s.b2;
            a1 = s.a1;
            a2 = s.a2;
        }
    };

    Biquad design(Band b, double gain_db, double freq_hz) const;
    bool active(Band b, double gain_db, double freq_hz) const;
    void apply_changes(); // audio thread: picks up new settings, then pending resets
    void reset_filters();

    // Written by the GUI (any order), read by the audio thread after `version`.
    array<atomic<double>, BANDS> gain{}; // dB (shelf/peak bands)
    array<atomic<double>, BANDS> freq{};
    atomic<int> type{PARAMETRIC};
    array<atomic<double>, ge7::SLIDER_COUNT> ge7_slider{};
    atomic<unsigned> version{1}; // bumped after every write
    atomic<bool> reset_pending{false};

    // Audio thread only.
    unsigned applied_version = 0;
    array<Biquad, BANDS> filters_l, filters_r;
    array<bool, BANDS> band_on{}; // band changes the signal (skip flat ones)
    int applied_type = PARAMETRIC;
    ge7::Coefficients ge7_coeffs;
    ge7::State ge7_state[2];
};

} // namespace namx
