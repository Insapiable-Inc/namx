#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "DelayLine.h"

/// Delay-line pitch shifter (mono, one per channel): two read taps sweep a
/// window through a short delay line at a rate set by the pitch ratio, each
/// faded by a Hann window half a period apart (the gains always sum to 1).
///
/// WSOLA alignment: when a tap restarts its sweep (silent at that moment),
/// its new start is moved by up to ±12 ms to the position whose recent
/// waveform best matches what the other tap is playing, so the crossfade
/// joins two in-phase signals instead of beating (plain two-tap shifters
/// cancel whole frequencies). Latency ≈ half the window (25 ms at 50 ms).
/// Allocates only in prepare() (non-RT).
class PitchShifter
{
public:
    void prepare(double sr, float window_ms = 50.0f) {
        m_window = std::max(64.0f, static_cast<float>(sr) * window_ms / 1000.0f);
        m_search = std::max(8, static_cast<int>(sr * 0.012));
        m_corr_len = std::max<size_t>(32, static_cast<size_t>(sr * 0.005));
        m_line.resize(static_cast<size_t>(m_window) * 2 + static_cast<size_t>(m_search) * 2 +
                      m_corr_len + 8);
        for (size_t i = 0; i < K_TABLE; ++i) {
            const double s = std::sin(3.14159265358979323846 * static_cast<double>(i) / K_TABLE);
            m_hann_table[i] = static_cast<float>(s * s); // sin²: tap A + tap B (½ period apart) = 1
        }
        reset();
    }

    void reset() {
        m_line.clear();
        m_phase = 0.0f;
        m_tap_offsets[0] = m_tap_offsets[1] = 0.0f;
        m_prev[0] = 0.0f;
        m_prev[1] = 0.5f;
    }

    /// Semitones (fractional allowed); 0 = passthrough delay.
    void set_semitones(float st) {
        m_ratio = std::pow(2.0f, st / 12.0f);
    }

    float process(float x) {
        if (m_line.empty()) {
            return x;
        }
        m_line.push(x);

        // The delay changes at (1 − ratio) samples per sample: shrinking
        // delay = reading faster = pitch up.
        m_phase += (1.0f - m_ratio) / m_window;
        m_phase -= std::floor(m_phase);
        const float p_b = m_phase + 0.5f >= 1.0f ? m_phase - 0.5f : m_phase + 0.5f;

        // A wrap = the tap just jumped to the other end of its sweep (at
        // zero gain): re-align it against the other tap.
        const float p[2] = {m_phase, p_b};
        for (int k = 0; k < 2; ++k) {
            if (std::abs(p[k] - m_prev[k]) > 0.5f) {
                m_tap_offsets[k] = align(p[k], p[1 - k], m_tap_offsets[1 - k]);
            }
            m_prev[k] = p[k];
        }

        const float out =
            tap(p[0], m_tap_offsets[0]) * hann(p[0]) + tap(p[1], m_tap_offsets[1]) * hann(p[1]);
        return out;
    }

private:
    static constexpr size_t K_TABLE = 1024;

    float hann(float p) const {
        return m_hann_table[static_cast<size_t>(p * K_TABLE) & (K_TABLE - 1)];
    }

    float delay_of(float p, float off) const {
        return 1.0f + p * m_window + off;
    }

    // Best offset in [−search, +search] (kept ≥ 0 delay) for a restarting
    // tap: maximise the normalised correlation between the last mCorrLen
    // samples behind its new read point and behind the other tap's.
    float align(float p, float p_other, float off_other) const {
        const float d_other = delay_of(p_other, off_other);
        const float d_base = delay_of(p, 0.0f);
        auto at = [&](float d, size_t j) {
            return m_line.at(static_cast<size_t>(d) + j);
        };
        auto score = [&](int off) {
            const float d = d_base + static_cast<float>(off);
            if (d < 1.0f ||
                d + static_cast<float>(m_corr_len) + 2.0f >= static_cast<float>(m_line.size())) {
                return -1e30f;
            }
            float xy = 0.0f, yy = 1e-9f;
            for (size_t j = 0; j < m_corr_len; j += 2) // every other sample: plenty for alignment
            {
                const float a = at(d, j), b = at(d_other, j);
                xy += a * b;
                yy += a * a;
            }
            return xy / std::sqrt(yy);
        };
        int best = 0;
        float best_s = score(0);
        for (int off = -m_search; off <= m_search; off += 4) // coarse
        {
            const float sc = score(off);
            if (sc > best_s) {
                best_s = sc;
                best = off;
            }
        }
        const int c = best;
        for (int off = c - 3; off <= c + 3; ++off) // refine
        {
            const float sc = score(off);
            if (sc > best_s) {
                best_s = sc;
                best = off;
            }
        }
        return static_cast<float>(best);
    }

    float tap(float p, float off) const {
        return m_line.interpolate(delay_of(p, off));
    }

    DelayLine m_line;
    std::array<float, K_TABLE> m_hann_table{};
    float m_phase = 0.0f;
    float m_window = 2400.0f;
    float m_ratio = 1.0f;
    int m_search = 576;
    size_t m_corr_len = 240;
    float m_tap_offsets[2]{}; // per-tap alignment offset (samples)
    float m_prev[2]{0.0f, 0.5f}; // last phase per tap (wrap detection)
};
