#include "EQProcessor.h"

#include <algorithm>
#include <numbers>
#include <utility>

#include "Common.h"

namespace namx
{

EQProcessor::EQProcessor() {
    static constexpr array<double, BANDS> DEFAULT_FREQ{20.0,   250.0,  1000.0,
                                                       3000.0, 4000.0, 20000.0};
    for (size_t b = 0; b < BANDS; ++b) {
        gain[b].store(0.0);
        freq[b].store(DEFAULT_FREQ[b]);
    }
}

void EQProcessor::on_sample_rate_changed(double) {
    // Called with processing suspended. The coefficients depend on the rate, and the
    // old filter state belongs to the old rate: recompute and start clean.
    reset_pending.store(true);
    version.fetch_add(1, std::memory_order_release);
}

void EQProcessor::set_gain(Band b, double db) {
    gain[std::to_underlying(b)].store(db, std::memory_order_relaxed);
    version.fetch_add(1, std::memory_order_release);
}

void EQProcessor::set_freq(Band b, double hz) {
    freq[std::to_underlying(b)].store(hz, std::memory_order_relaxed);
    version.fetch_add(1, std::memory_order_release);
}

bool EQProcessor::active(Band b, double gain_db, double freq_hz) const {
    switch (b) {
        case Band::LOW_CUT:
            return freq_hz > 20.5;
        case Band::HIGH_CUT:
            return freq_hz < 19999.0 && freq_hz < 0.45 * sample_rate;
        default:
            return std::abs(gain_db) > 0.01;
    }
}

void EQProcessor::apply_changes() {
    const unsigned v = version.load(std::memory_order_acquire);
    if (v != applied_version) {
        applied_version = v;
        for (size_t i = 0; i < BANDS; ++i) {
            const Band b = static_cast<Band>(i);
            const double g = gain[i].load(std::memory_order_relaxed);
            const double f = freq[i].load(std::memory_order_relaxed);
            const Biquad c = design(b, g, f);
            filters_l[i].set_coeffs(c);
            filters_r[i].set_coeffs(c);
            const bool on = active(b, g, f);
            if (on && !band_on[i]) { // start a band from silence
                filters_l[i].reset();
                filters_r[i].reset();
            }
            band_on[i] = on;
        }
    }
    if (reset_pending.exchange(false, std::memory_order_acquire)) {
        reset_filters();
    }
}

EQProcessor::Biquad EQProcessor::design(Band band, double gain_db, double freq_hz) const {
    Biquad bq;
    const double fs = sample_rate;
    const double f = std::clamp(freq_hz, 10.0, 0.45 * fs);
    const double w0 = 2.0 * std::numbers::pi * f / fs;
    const double cw = std::cos(w0), sw = std::sin(w0);
    double b0, b1, b2, a0, a1, a2;

    switch (band) {
        case Band::LOW_CUT:
        case Band::HIGH_CUT: {
            const double alpha = sw / (2.0 * 0.7071); // Butterworth Q
            if (band == Band::LOW_CUT) {
                b0 = (1 + cw) / 2;
                b1 = -(1 + cw);
                b2 = (1 + cw) / 2;
            } else {
                b0 = (1 - cw) / 2;
                b1 = (1 - cw);
                b2 = (1 - cw) / 2;
            }
            a0 = 1 + alpha;
            a1 = -2 * cw;
            a2 = 1 - alpha;
            break;
        }
        case Band::MIDDLE:
        case Band::HIGH_MID: {
            const double a = std::pow(10.0, gain_db / 40.0);
            const double alpha = sw / (2.0 * (band == Band::MIDDLE ? 0.707 : 1.0));
            b0 = 1 + alpha * a;
            b1 = -2 * cw;
            b2 = 1 - alpha * a;
            a0 = 1 + alpha / a;
            a1 = -2 * cw;
            a2 = 1 - alpha / a;
            break;
        }
        case Band::BASS:
        case Band::TREBLE:
        default: {
            const double a = std::pow(10.0, gain_db / 40.0);
            const double alpha = sw / 2.0 * std::sqrt(2.0); // shelf slope S = 1
            const double s_a = 2.0 * std::sqrt(a) * alpha;
            if (band == Band::BASS) {
                b0 = a * ((a + 1) - (a - 1) * cw + s_a);
                b1 = 2 * a * ((a - 1) - (a + 1) * cw);
                b2 = a * ((a + 1) - (a - 1) * cw - s_a);
                a0 = (a + 1) + (a - 1) * cw + s_a;
                a1 = -2 * ((a - 1) + (a + 1) * cw);
                a2 = (a + 1) + (a - 1) * cw - s_a;
            } else {
                b0 = a * ((a + 1) + (a - 1) * cw + s_a);
                b1 = -2 * a * ((a - 1) + (a + 1) * cw);
                b2 = a * ((a + 1) + (a - 1) * cw - s_a);
                a0 = (a + 1) - (a - 1) * cw + s_a;
                a1 = 2 * ((a - 1) - (a + 1) * cw);
                a2 = (a + 1) - (a - 1) * cw - s_a;
            }
            break;
        }
    }
    bq.b0 = b0 / a0;
    bq.b1 = b1 / a0;
    bq.b2 = b2 / a0;
    bq.a1 = a1 / a0;
    bq.a2 = a2 / a0;
    return bq;
}

bool EQProcessor::process(float* l, float* r, size_t n, bool input_is_stereo) {
    if (!enabled.load()) {
        return input_is_stereo;
    }

    apply_changes();
    if (input_is_stereo && !right_fed) {
        sync_right_channel(); // see EffectBase::process
    }
    right_fed = input_is_stereo;

    for (size_t b = 0; b < BANDS; ++b) {
        if (!band_on[b]) {
            continue;
        }
        Biquad& filter_l = filters_l[b];
        for (size_t i = 0; i < n; ++i) {
            l[i] = filter_l.process(l[i]);
        }
        if (input_is_stereo) {
            Biquad& filter_r = filters_r[b];
            for (size_t i = 0; i < n; ++i) {
                r[i] = filter_r.process(r[i]);
            }
        }
    }
    if (!input_is_stereo) {
        std::copy(l, l + n, r);
    }

    return input_is_stereo;
}

void EQProcessor::sync_right_channel() {
    filters_r = filters_l; // the same coefficients; now the same history too
}

void EQProcessor::reset() { // audio thread (chain reorder), like process()
    reset_filters();
}

void EQProcessor::reset_filters() {
    for (Biquad& filter : filters_l) {
        filter.reset();
    }
    for (Biquad& filter : filters_r) {
        filter.reset();
    }
}

} // namespace namx
