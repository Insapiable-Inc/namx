#include "EQProcessor.h"

#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

EQProcessor::EQProcessor() {
    for (size_t b = 0; b < K_BANDS; ++b) {
        rebuild(Band(b));
    }
}

void EQProcessor::on_sample_rate_changed(double) {
    // Same lock discipline as the setters: the audio thread try_locks this
    // mutex, so coefficient/state rewrites must never happen bare.
    std::lock_guard<std::mutex> lock(m_mutex);
    for (size_t b = 0; b < K_BANDS; ++b) {
        rebuild(Band(b));
    }
    for (Biquad& filter : m_l) {
        filter.reset();
    }
    for (Biquad& filter : m_r) {
        filter.reset();
    }
}

void EQProcessor::set_gain(Band b, double d_b) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_gain[b] = d_b;
    rebuild(b);
}

void EQProcessor::set_freq(Band b, double hz) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_freq[b] = hz;
    rebuild(b);
}

bool EQProcessor::active(Band b) const {
    switch (b) {
        case LOW_CUT:
            return m_freq[b] > 20.5;
        case HIGH_CUT:
            return m_freq[b] < 19999.0 && m_freq[b] < 0.45 * m_sample_rate;
        default:
            return std::abs(m_gain[b]) > 0.01;
    }
}

void EQProcessor::rebuild(Band b) {
    const Biquad c = design(b);
    m_l[b].set_coeffs(c);
    m_r[b].set_coeffs(c);
    const bool on = active(b);
    if (on && !m_on[b]) {
        m_l[b].reset();
        m_r[b].reset();
    } // start a band from silence
    m_on[b] = on;
}

EQProcessor::Biquad EQProcessor::design(Band band) const {
    Biquad bq;
    const double fs = m_sample_rate;
    const double f = std::clamp(m_freq[band], 10.0, 0.45 * fs);
    const double w0 = 2.0 * M_PI * f / fs;
    const double cw = std::cos(w0), sw = std::sin(w0);
    double b0, b1, b2, a0, a1, a2;

    switch (band) {
        case LOW_CUT:
        case HIGH_CUT: {
            const double alpha = sw / (2.0 * 0.7071); // Butterworth Q
            if (band == LOW_CUT) {
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
        case MIDDLE:
        case HIGH_MID: {
            const double a = std::pow(10.0, m_gain[band] / 40.0);
            const double alpha = sw / (2.0 * (band == MIDDLE ? 0.707 : 1.0));
            b0 = 1 + alpha * a;
            b1 = -2 * cw;
            b2 = 1 - alpha * a;
            a0 = 1 + alpha / a;
            a1 = -2 * cw;
            a2 = 1 - alpha / a;
            break;
        }
        case BASS:
        case TREBLE:
        default: {
            const double a = std::pow(10.0, m_gain[band] / 40.0);
            const double alpha = sw / 2.0 * std::sqrt(2.0); // shelf slope S = 1
            const double s_a = 2.0 * std::sqrt(a) * alpha;
            if (band == BASS) {
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
    if (!m_enabled.load()) {
        return input_is_stereo;
    }

    // Skip this block if the GUI thread is updating coefficients (inaudible at normal buffer sizes)
    if (!m_mutex.try_lock()) {
        return input_is_stereo;
    }

    for (size_t b = 0; b < K_BANDS; ++b) {
        if (!m_on[b]) {
            continue;
        }
        Biquad& filter_l = m_l[b];
        for (size_t i = 0; i < n; ++i) {
            l[i] = filter_l.process(l[i]);
        }
        if (input_is_stereo) {
            Biquad& filter_r = m_r[b];
            for (size_t i = 0; i < n; ++i) {
                r[i] = filter_r.process(r[i]);
            }
        }
    }
    if (!input_is_stereo) {
        std::copy(l, l + n, r);
    }

    m_mutex.unlock();
    return input_is_stereo;
}

void EQProcessor::reset() {
    for (Biquad& filter : m_l) {
        filter.reset();
    }
    for (Biquad& filter : m_r) {
        filter.reset();
    }
}
