#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "Common.h"
#include "DelayLine.h"

namespace npl
{

// Delay-line pitch shifter (mono, one per channel): two read taps sweep a
// window through a short delay line at a rate set by the pitch ratio, each
// faded by a Hann window half a period apart (the gains always sum to 1).
//
// WSOLA alignment: when a tap restarts its sweep (silent at that moment),
// its new start is moved by up to ±12 ms to the position whose recent
// waveform best matches what the other tap is playing, so the crossfade
// joins two in-phase signals instead of beating (plain two-tap shifters
// cancel whole frequencies). Latency ≈ half the window (25 ms at 50 ms).
// Allocates only in prepare() (non-RT).
class PitchShifter
{
public:
    // Average delay of the shifted signal: half the window (the WSOLA re-alignment
    // moves it by up to ±12 ms around that).
    double latency_samples() const {
        return 0.5 * static_cast<double>(window);
    }

    void prepare(double sr, float window_ms = 50.0f) {
        window = std::max(64.0f, static_cast<float>(sr) * window_ms / 1000.0f);
        search = std::max(8, static_cast<int>(sr * 0.012));
        corr_len = std::max<size_t>(32, static_cast<size_t>(sr * 0.005));
        line.resize(static_cast<size_t>(window) * 2 + static_cast<size_t>(search) * 2 + corr_len +
                    8);
        for (size_t i = 0; i < TABLE; ++i) {
            const double s = std::sin(3.14159265358979323846 * static_cast<double>(i) / TABLE);
            hann_table[i] = static_cast<float>(s * s); // sin²: tap A + tap B (½ period apart) = 1
        }
        reset();
    }

    void reset() {
        line.clear();
        phase = 0.0f;
        tap_offsets[0] = tap_offsets[1] = 0.0f;
        prev[0] = 0.0f;
        prev[1] = 0.5f;
    }

    // Semitones (fractional allowed); 0 = passthrough delay.
    void set_semitones(float st) {
        ratio = std::pow(2.0f, st / 12.0f);
    }

    float process(float x) {
        if (line.empty()) {
            return x;
        }
        line.push(x);

        // The delay changes at (1 − ratio) samples per sample: shrinking
        // delay = reading faster = pitch up.
        phase += (1.0f - ratio) / window;
        phase -= std::floor(phase);
        const float p_b = phase + 0.5f >= 1.0f ? phase - 0.5f : phase + 0.5f;

        // A wrap = the tap just jumped to the other end of its sweep (at
        // zero gain): re-align it against the other tap.
        const float p[2] = {phase, p_b};
        for (int k = 0; k < 2; ++k) {
            if (std::abs(p[k] - prev[k]) > 0.5f) {
                tap_offsets[k] = align(p[k], p[1 - k], tap_offsets[1 - k]);
            }
            prev[k] = p[k];
        }

        const float out =
            tap(p[0], tap_offsets[0]) * hann(p[0]) + tap(p[1], tap_offsets[1]) * hann(p[1]);
        return out;
    }

private:
    static constexpr size_t TABLE = 1024;

    float hann(float p) const {
        return hann_table[static_cast<size_t>(p * TABLE) & (TABLE - 1)];
    }

    float delay_of(float p, float off) const {
        return 1.0f + p * window + off;
    }

    // Best offset in [−search, +search] (kept ≥ 0 delay) for a restarting
    // tap: maximise the normalised correlation between the last corr_len
    // samples behind its new read point and behind the other tap's.
    // Normalised correlation between the corr_len samples behind delay `d` and
    // behind `d_other`; very low when `d` is out of range.
    float alignment_score(float d, float d_other) const {
        if (d < 1.0f ||
            d + static_cast<float>(corr_len) + 2.0f >= static_cast<float>(line.size())) {
            return -1e30f;
        }
        float xy = 0.0f, yy = 1e-9f;
        for (size_t j = 0; j < corr_len; j += 2) // every other sample: plenty for alignment
        {
            const float a = line.at(static_cast<size_t>(d) + j);
            const float b = line.at(static_cast<size_t>(d_other) + j);
            xy += a * b;
            yy += a * a;
        }
        return xy / std::sqrt(yy);
    }

    float align(float p, float p_other, float off_other) const {
        const float d_other = delay_of(p_other, off_other);
        const float d_base = delay_of(p, 0.0f);
        int best = 0;
        float best_s = alignment_score(d_base, d_other);
        for (int off = -search; off <= search; off += 4) // coarse
        {
            const float sc = alignment_score(d_base + static_cast<float>(off), d_other);
            if (sc > best_s) {
                best_s = sc;
                best = off;
            }
        }
        const int c = best;
        for (int off = c - 3; off <= c + 3; ++off) // refine
        {
            const float sc = alignment_score(d_base + static_cast<float>(off), d_other);
            if (sc > best_s) {
                best_s = sc;
                best = off;
            }
        }
        return static_cast<float>(best);
    }

    float tap(float p, float off) const {
        return line.interpolate(delay_of(p, off));
    }

    DelayLine line;
    array<float, TABLE> hann_table{};
    float phase = 0.0f;
    float window = 2400.0f;
    float ratio = 1.0f;
    int search = 576;
    size_t corr_len = 240;
    float tap_offsets[2]{}; // per-tap alignment offset (samples)
    float prev[2]{0.0f, 0.5f}; // last phase per tap (wrap detection)
};

} // namespace npl
