#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <mutex>

#include "EffectBase.h"

/// 4-band semi-parametric EQ + low/high cut (Audio EQ Cookbook biquads):
///   Bass     low shelf    40–500 Hz   (default 250 — the old fixed band)
///   Middle   peak, Q 0.7  200–2.5 kHz (default 1 kHz)
///   High-Mid peak, Q 1.0  800–8 kHz   (default 3 kHz)
///   Treble   high shelf   1.5–12 kHz  (default 4 kHz)
///   Low Cut  2nd-order high-pass 20–500 Hz (20 = off)
///   High Cut 2nd-order low-pass 2–20 kHz (20 kHz = off)
/// GUI setters rebuild coefficients under a mutex; the audio thread
/// try_locks and skips a block rather than wait. Independent L/R state.
class EQProcessor : public EffectBase
{
public:
    enum Band
    {
        LOW_CUT = 0,
        BASS,
        MIDDLE,
        HIGH_MID,
        TREBLE,
        HIGH_CUT,
        K_BANDS
    };

    EQProcessor();

    bool process(float* l, float* r, size_t n, bool input_is_stereo) override;
    void reset() override;

    void set_gain(Band b, double db);
    void set_freq(Band b, double hz);

    // Back-compat names (3-band era).
    void set_low_gain(double db) {
        set_gain(BASS, db);
    }
    void set_mid_gain(double db) {
        set_gain(MIDDLE, db);
    }
    void set_treble_gain(double db) {
        set_gain(TREBLE, db);
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

    Biquad design(Band b) const;
    void rebuild(Band b); // caller holds mMutex
    bool active(Band b) const;

    std::array<double, K_BANDS> m_gain{}; // dB (shelf/peak bands)
    std::array<double, K_BANDS> m_freq{20.0, 250.0, 1000.0, 3000.0, 4000.0, 20000.0};
    std::array<Biquad, K_BANDS> m_filters_l, m_filters_r;
    std::array<bool, K_BANDS> m_band_on{}; // band changes the signal (skip flat ones)

    std::mutex m_mutex; // GUI writes coefficients; audio thread uses try_lock
};
