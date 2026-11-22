#pragma once

#include <array>
#include <atomic>
#include <cmath>

#include "Common.h"
#include "EffectBase.h"
#include "Ge7Eq.h"

namespace namx
{

// Equalizer: a model of a Boss GE-7 Equalizer (Ge7Eq.h) — its seven bands and Level in dB
// (−15…+15), each band's frequency movable an octave either way — then a Low Cut and a High
// Cut (2nd-order Butterworth, Audio EQ Cookbook; 20 Hz / 20 kHz = off).
// Lock-free: the GUI only stores the settings in atomics and bumps a version; the audio
// thread owns every filter (coefficients and state) and recomputes the coefficients at the
// top of the next block — no mutex, no skipped blocks, no shared filter state (code review
// items 04, 18). Independent L/R state.
class EQProcessor : public EffectBase
{
public:
    enum class Cut : size_t
    {
        LOW = 0,
        HIGH
    };
    static constexpr size_t CUTS = 2;

    EQProcessor();

    bool process(float* l, float* r, size_t n, bool input_is_stereo) override;
    void reset() override;
    void sync_right_channel() override;

    void set_cut(Cut c, double hz);
    // The sliders: 0–6 the bands (91 Hz … 3.3 kHz, then the shelf), 7 Level; dB.
    void set_slider(size_t slider, double db);
    // A band's frequency (ge7::BAND_HZ by default, an octave either way).
    void set_frequency(size_t band, double hz);

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

    Biquad design(Cut c, double freq_hz) const;
    bool active(Cut c, double freq_hz) const;
    void apply_changes(); // audio thread: picks up new settings, then pending resets
    void reset_filters();

    // Written by the GUI (any order), read by the audio thread after `version`.
    array<atomic<double>, CUTS> cut_hz{};
    array<atomic<double>, ge7::SLIDER_COUNT> slider{};
    array<atomic<double>, ge7::BAND_COUNT> frequency{};
    atomic<unsigned> version{1}; // bumped after every write
    atomic<bool> reset_pending{false};

    // Audio thread only.
    unsigned applied_version = 0;
    array<Biquad, CUTS> cuts_l, cuts_r;
    array<bool, CUTS> cut_on{}; // the cut changes the signal (skip open ones)
    ge7::Coefficients coeffs;
    ge7::State state[2];
};

} // namespace namx
