#include "EQProcessor.h"

#include <algorithm>
#include <numbers>
#include <utility>

#include "Common.h"

namespace namx
{

EQProcessor::EQProcessor() {
    cut_hz[std::to_underlying(Cut::LOW)].store(20.0);
    cut_hz[std::to_underlying(Cut::HIGH)].store(20000.0);
    for (size_t band = 0; band < ge7::BAND_COUNT; ++band) {
        frequency[band].store(ge7::BAND_HZ[band]);
    }
}

void EQProcessor::on_sample_rate_changed(double) {
    // Called with processing suspended. The coefficients depend on the rate, and the
    // old filter state belongs to the old rate: recompute and start clean.
    reset_pending.store(true);
    version.fetch_add(1, std::memory_order_release);
}

void EQProcessor::set_cut(Cut c, double hz) {
    cut_hz[std::to_underlying(c)].store(hz, std::memory_order_relaxed);
    version.fetch_add(1, std::memory_order_release);
}

void EQProcessor::set_slider(size_t index, double db) {
    if (index >= ge7::SLIDER_COUNT) {
        return;
    }
    slider[index].store(std::clamp(db, ge7::SLIDER_MIN, ge7::SLIDER_MAX),
                        std::memory_order_relaxed);
    version.fetch_add(1, std::memory_order_release);
}

void EQProcessor::set_frequency(size_t band, double hz) {
    if (band >= ge7::BAND_COUNT) {
        return;
    }
    frequency[band].store(std::clamp(hz, ge7::BAND_HZ[band] / ge7::FREQUENCY_SPAN,
                                     ge7::BAND_HZ[band] * ge7::FREQUENCY_SPAN),
                          std::memory_order_relaxed);
    version.fetch_add(1, std::memory_order_release);
}

bool EQProcessor::active(Cut c, double freq_hz) const {
    return c == Cut::LOW ? freq_hz > 20.5 : freq_hz < 19999.0 && freq_hz < 0.45 * sample_rate;
}

void EQProcessor::apply_changes() {
    const unsigned v = version.load(std::memory_order_acquire);
    if (v != applied_version) {
        applied_version = v;
        array<double, ge7::SLIDER_COUNT> sliders{};
        for (size_t i = 0; i < sliders.size(); ++i) {
            sliders[i] = slider[i].load(std::memory_order_relaxed);
        }
        array<double, ge7::BAND_COUNT> hz{};
        for (size_t i = 0; i < hz.size(); ++i) {
            hz[i] = frequency[i].load(std::memory_order_relaxed);
        }
        coeffs = ge7::design(sliders, hz, sample_rate);
        for (size_t i = 0; i < CUTS; ++i) {
            const Cut c = static_cast<Cut>(i);
            const double f = cut_hz[i].load(std::memory_order_relaxed);
            const Biquad k = design(c, f);
            cuts_l[i].set_coeffs(k);
            cuts_r[i].set_coeffs(k);
            const bool on = active(c, f);
            if (on && !cut_on[i]) { // start a cut from silence
                cuts_l[i].reset();
                cuts_r[i].reset();
            }
            cut_on[i] = on;
        }
    }
    if (reset_pending.exchange(false, std::memory_order_acquire)) {
        reset_filters();
    }
}

EQProcessor::Biquad EQProcessor::design(Cut c, double freq_hz) const {
    const double fs = sample_rate;
    const double f = std::clamp(freq_hz, 10.0, 0.45 * fs);
    const double w0 = 2.0 * std::numbers::pi * f / fs;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * 0.7071); // Butterworth Q
    double b0, b1, b2;
    if (c == Cut::LOW) {
        b0 = (1 + cw) / 2;
        b1 = -(1 + cw);
        b2 = (1 + cw) / 2;
    } else {
        b0 = (1 - cw) / 2;
        b1 = (1 - cw);
        b2 = (1 - cw) / 2;
    }
    const double a0 = 1 + alpha;
    const double a1 = -2 * cw;
    const double a2 = 1 - alpha;
    Biquad bq;
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

    for (size_t i = 0; i < n; ++i) {
        l[i] = ge7::process(coeffs, state[0], l[i]);
    }
    if (input_is_stereo) {
        for (size_t i = 0; i < n; ++i) {
            r[i] = ge7::process(coeffs, state[1], r[i]);
        }
    }
    for (size_t c = 0; c < CUTS; ++c) {
        if (!cut_on[c]) {
            continue;
        }
        Biquad& filter_l = cuts_l[c];
        for (size_t i = 0; i < n; ++i) {
            l[i] = filter_l.process(l[i]);
        }
        if (input_is_stereo) {
            Biquad& filter_r = cuts_r[c];
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
    cuts_r = cuts_l; // the same coefficients; now the same history too
    state[1] = state[0];
}

void EQProcessor::reset() { // audio thread (chain reorder), like process()
    reset_filters();
}

void EQProcessor::reset_filters() {
    state[0].reset();
    state[1].reset();
    for (Biquad& filter : cuts_l) {
        filter.reset();
    }
    for (Biquad& filter : cuts_r) {
        filter.reset();
    }
}

} // namespace namx
