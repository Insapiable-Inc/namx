#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>

#include "Common.h"

namespace namx
{

// Low-latency polyphonic pitch shifter (mono, one per channel), after the filter-bank
// and ring-modulation idea of Juillerat, Schubiger-Banz and Müller Arisona (2008).
//
//   input → complex 4th-order gammatone bank (two bands per ERB) → each band rotated by
//   a phasor turning at (ratio − 1) × the frequency of the spectral peak the band
//   belongs to → each band delayed and phase-aligned (Hohmann 2002) → real part of the
//   weighted sum.
//
// Hohmann's resynthesis delays every band so its impulse response peaks at the same
// moment (DELAY_MS) with zero phase there; bands that peak later than that (the
// lowest ones, whose narrow bandwidth makes them slow) keep their own delay, so the
// bass arrives later than the rest (~20 ms at the low E string). Rotating a band moves
// everything in it by a fixed number of hertz; rotating every band around a peak at
// that peak's own rate moves the partial there to exactly ratio × its frequency, and
// keeps the bands sharing it coherent. Frequencies and peaks are re-estimated every
// CONTROL samples; band phasors glide to their peak's over PULL_MS.
//
// Chosen by ear over WSOLA, and over denser/narrower/wider band layouts, smoother
// control, phase reset at attacks and leak correction (2026-10-03, Phase 22). Known
// limit: a faint beat on long held chords — a little of each partial leaks into the
// neighbouring partial's bands and is shifted by that partial's amount.
// Allocates only in prepare() (non-RT).
class FilterBankShifter
{
public:
    // Latency where it's longest and matters most: at the low E string (82 Hz). Higher
    // up it falls to DELAY_MS.
    double latency_samples() const {
        return static_cast<double>(bass_latency);
    }

    void prepare(double sr) {
        rate = sr;
        build_bank();
        reset();
    }

    void reset() {
        for (Block& b : blocks) {
            for (auto member :
                 {&Block::s1r, &Block::s1i, &Block::s2r, &Block::s2i, &Block::s3r, &Block::s3i,
                  &Block::z_r, &Block::z_i, &Block::c_r, &Block::c_i, &Block::energy, &Block::rot_i,
                  &Block::step_i, &Block::out_r, &Block::out_i}) {
                std::fill(std::begin(b.*member), std::end(b.*member), 0.0f);
            }
            std::fill(std::begin(b.rot_r), std::end(b.rot_r), 1.0f);
            std::fill(std::begin(b.step_r), std::end(b.step_r), 1.0f);
        }
        for (auto* v : {&smoothed_energy, &omega, &line_r, &line_i}) {
            std::fill(v->begin(), v->end(), 0.0f);
        }
        counter = 0;
        line_pos = 0;
    }

    // Semitones (fractional allowed).
    void set_semitones(float st) {
        const float new_ratio = std::pow(2.0f, st / 12.0f);
        if (new_ratio != ratio) {
            ratio = new_ratio;
            update_weights();
        }
    }

    // In place.
    void process(float* buf, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            run_bands(buf[i], &line_r[line_pos * bands], &line_i[line_pos * bands]);
            float y = 0.0f;
            for (size_t k = 0; k < bands; ++k) {
                size_t pos = line_pos + line_len - delay[k];
                if (pos >= line_len) {
                    pos -= line_len;
                }
                y += wr[k] * line_r[pos * bands + k] - wi[k] * line_i[pos * bands + k];
            }
            buf[i] = y;
            if (++line_pos == line_len) {
                line_pos = 0;
            }
            if (++counter == CONTROL) {
                counter = 0;
                control();
            }
        }
    }

private:
    static constexpr int CONTROL = 16; // samples between frequency/peak updates
    static constexpr double BANDS_PER_ERB = 2.0;
    static constexpr double BANDWIDTH_FACTOR = 0.7; // × 1.019 ERB (Hohmann's is 1)
    static constexpr double DELAY_MS = 4.0;
    static constexpr double LOWEST_HZ = 40.0;
    static constexpr float PULL_MS = 5.0f; // band phasors glide to their peak's
    static constexpr float SMOOTH = 0.5f; // energy smoothing between control steps
    static constexpr float SAME_PARTIAL = 0.02f; // relative frequency match
    static constexpr double PI = 3.14159265358979323846;

    static double erb_number(double hz) {
        return 21.4 * std::log10(1.0 + 0.00437 * hz);
    }
    static double erb_to_hz(double erb) {
        return (std::pow(10.0, erb / 21.4) - 1.0) / 0.00437;
    }
    static double erb_width(double hz) {
        return 24.7 + hz / 9.265;
    }

    // Per-sample band state, eight bands per block: one base pointer, fixed-size inner
    // loops — the compiler vectorizes them (separate arrays defeated its alias checks).
    static constexpr size_t LANES = 8;
    struct alignas(32) Block
    {
        float a_r[LANES], a_i[LANES], gain[LANES]; // pole, input gain
        float s1r[LANES], s1i[LANES], s2r[LANES], s2i[LANES], s3r[LANES], s3i[LANES];
        float z_r[LANES], z_i[LANES]; // band output (4th pole)
        float c_r[LANES], c_i[LANES], energy[LANES]; // per-control-block accumulators
        float rot_r[LANES], rot_i[LANES], step_r[LANES], step_i[LANES]; // phasor, increment
        float out_r[LANES], out_i[LANES]; // rotated output for the delay line
    };
    float& field(float (Block::*member)[LANES], size_t k) {
        return (blocks[k / LANES].*member)[k % LANES];
    }

    // One input sample through every band: filter, measure, rotate, then into the
    // bands' delay-line slots.
    void run_bands(float x, float* line_out_r, float* line_out_i) {
        for (Block& b : blocks) {
            for (size_t j = 0; j < LANES; ++j) {
                // Four complex one-poles in series (the first one takes the real input).
                const float a_r = b.a_r[j], a_i = b.a_i[j];
                float t_r = a_r * b.s1r[j] - a_i * b.s1i[j] + b.gain[j] * x;
                float t_i = a_r * b.s1i[j] + a_i * b.s1r[j];
                b.s1r[j] = t_r;
                b.s1i[j] = t_i;
                const float u_r = a_r * b.s2r[j] - a_i * b.s2i[j] + t_r;
                const float u_i = a_r * b.s2i[j] + a_i * b.s2r[j] + t_i;
                b.s2r[j] = u_r;
                b.s2i[j] = u_i;
                t_r = a_r * b.s3r[j] - a_i * b.s3i[j] + u_r;
                t_i = a_r * b.s3i[j] + a_i * b.s3r[j] + u_i;
                b.s3r[j] = t_r;
                b.s3i[j] = t_i;
                const float old_r = b.z_r[j], old_i = b.z_i[j];
                const float new_r = a_r * old_r - a_i * old_i + t_r;
                const float new_i = a_r * old_i + a_i * old_r + t_i;
                b.z_r[j] = new_r;
                b.z_i[j] = new_i;
                // z[n]·conj(z[n−1]) accumulates the band's frequency.
                b.c_r[j] += new_r * old_r + new_i * old_i;
                b.c_i[j] += new_i * old_r - new_r * old_i;
                b.energy[j] += new_r * new_r + new_i * new_i;
                // Rotate.
                const float q_r = b.rot_r[j] * b.step_r[j] - b.rot_i[j] * b.step_i[j];
                const float q_i = b.rot_r[j] * b.step_i[j] + b.rot_i[j] * b.step_r[j];
                b.rot_r[j] = q_r;
                b.rot_i[j] = q_i;
                b.out_r[j] = new_r * q_r - new_i * q_i;
                b.out_i[j] = new_r * q_i + new_i * q_r;
            }
        }
        for (size_t k = 0; k < bands; ++k) {
            line_out_r[k] = blocks[k / LANES].out_r[k % LANES];
            line_out_i[k] = blocks[k / LANES].out_i[k % LANES];
        }
    }

    void build_bank() {
        using cd = std::complex<double>;
        const double top_hz = std::min(20000.0, 0.45 * rate);
        const double lo = erb_number(LOWEST_HZ), hi = erb_number(top_hz);
        bands = static_cast<size_t>((hi - lo) * BANDS_PER_ERB) + 1;
        blocks.assign((bands + LANES - 1) / LANES, Block{}); // padding lanes stay silent
        for (auto* v : {&center, &smoothed_energy, &omega, &wr, &wi, &pending_r, &pending_i}) {
            v->assign(bands, 0.0f);
        }
        owner.assign(bands, 0);
        delay.assign(bands, 0);
        target_delay = static_cast<size_t>(DELAY_MS * 0.001 * rate);

        double bass_hz = 0.0;
        vector<cd> poles(bands);
        vector<double> input_gains(bands);
        vector<cd> phases(bands);
        const size_t ir_len = static_cast<size_t>(0.15 * rate);
        size_t longest = 0;
        for (size_t k = 0; k < bands; ++k) {
            const double hz = erb_to_hz(lo + static_cast<double>(k) / BANDS_PER_ERB);
            const double b = BANDWIDTH_FACTOR * 1.019 * erb_width(hz);
            const double lambda = std::exp(-2.0 * PI * b / rate);
            const double w = 2.0 * PI * hz / rate;
            center[k] = static_cast<float>(w);
            const cd a = std::polar(lambda, w);
            field(&Block::a_r, k) = static_cast<float>(a.real());
            field(&Block::a_i, k) = static_cast<float>(a.imag());
            // 2·(1 − λ)⁴: unity at the centre, and the real part of the (nearly analytic)
            // output carries the input's full amplitude.
            const double g = 2.0 * std::pow(1.0 - lambda, 4);
            field(&Block::gain, k) = static_cast<float>(g);
            poles[k] = a;
            input_gains[k] = g;
            // Impulse response, to place the envelope peak and read its phase there.
            cd st[4]{};
            double peak = -1.0;
            size_t peak_at = 0;
            cd at_peak = 1.0;
            for (size_t n = 0; n < ir_len; ++n) {
                st[0] = a * st[0] + (n == 0 ? cd(g, 0.0) : cd(0.0, 0.0));
                for (int s = 1; s < 4; ++s) {
                    st[s] = a * st[s] + st[s - 1];
                }
                if (std::abs(st[3]) > peak) {
                    peak = std::abs(st[3]);
                    peak_at = n;
                    at_peak = st[3];
                }
            }
            delay[k] = peak_at < target_delay ? static_cast<uint32_t>(target_delay - peak_at) : 0;
            if (std::abs(hz - 82.41) < std::abs(bass_hz - 82.41)) {
                bass_hz = hz;
                bass_latency = std::max(peak_at, target_delay);
            }
            longest = std::max<size_t>(longest, delay[k]);
            // The band's (delayed) response is real and positive at its peak.
            phases[k] = std::conj(at_peak) / std::abs(at_peak);
        }
        // Band k's exact frequency response, g / (1 − a·e^{−jω})⁴, with its delay.
        auto response = [&](size_t k, double w) {
            const cd den = 1.0 - poles[k] * std::polar(1.0, -w);
            return input_gains[k] / (den * den * den * den) * std::polar(1.0, -w * delay[k]);
        };
        // Bands that peak later than the target can't be aligned in time; their
        // neighbours would then meet half a cycle apart somewhere between their centres
        // (deep notches). Instead, from the top down, each band's phase is set so it
        // agrees with the band above it exactly at their crossover frequency.
        for (size_t k = bands - 1; k-- > 0;) {
            if (delay[k] > 0 && delay[k + 1] > 0) {
                continue; // both time-aligned already
            }
            const double crossover = 0.5 * (center[k] + center[k + 1]);
            const cd above = response(k + 1, crossover) * phases[k + 1];
            const cd here = response(k, crossover);
            phases[k] = std::polar(1.0, std::arg(above) - std::arg(here));
        }
        line_len = longest + 1;
        line_r.assign(line_len * bands, 0.0f);
        line_i.assign(line_len * bands, 0.0f);

        // Flat overall response: the summed bank's response at every band centre,
        // divided out a few times over (GUI thread, once per rate).
        vector<vector<cd>> table(bands, vector<cd>(bands));
        for (size_t m = 0; m < bands; ++m) {
            for (size_t k = 0; k < bands; ++k) {
                // Re(·) of a one-sided response: half.
                table[m][k] = 0.5 * response(k, center[m]) * phases[k];
            }
        }
        vector<double> gains(bands, 1.0);
        for (int pass = 0; pass < 20; ++pass) {
            for (size_t m = 0; m < bands; ++m) {
                cd sum = 0.0;
                for (size_t k = 0; k < bands; ++k) {
                    sum += gains[k] * table[m][k];
                }
                gains[m] *= std::sqrt(1.0 / std::max(1e-6, std::abs(sum)));
            }
        }
        base_w.resize(bands);
        for (size_t k = 0; k < bands; ++k) {
            base_w[k] = gains[k] * phases[k];
        }
        update_weights();
    }

    // Upward shifts would push the top bands past Nyquist (they'd fold back as
    // inharmonic noise): those bands are faded out.
    void update_weights() {
        const float limit = 0.9f * static_cast<float>(PI);
        for (size_t k = 0; k < bands; ++k) {
            const float shifted = center[k] * ratio;
            const float fade = std::clamp((limit - shifted) / (0.05f * limit), 0.0f, 1.0f);
            wr[k] = static_cast<float>(base_w[k].real()) * fade;
            wi[k] = static_cast<float>(base_w[k].imag()) * fade;
        }
    }

    void control() {
        for (size_t k = 0; k < bands; ++k) {
            float& e = field(&Block::energy, k);
            float& c_r = field(&Block::c_r, k);
            float& c_i = field(&Block::c_i, k);
            smoothed_energy[k] = SMOOTH * smoothed_energy[k] + (1.0f - SMOOTH) * e;
            omega[k] = std::atan2(c_i, c_r);
            c_r = c_i = e = 0.0f;
        }
        // Each band follows the peak it climbs to.
        for (size_t k = 0; k < bands; ++k) {
            size_t j = k;
            for (;;) {
                const float here = smoothed_energy[j];
                const float left = j > 0 ? smoothed_energy[j - 1] : -1.0f;
                const float right = j + 1 < bands ? smoothed_energy[j + 1] : -1.0f;
                // Climb only toward a neighbour hearing the same partial (frequency
                // within SAME_PARTIAL): different partials never lock together.
                const bool right_ok = j + 1 < bands && std::abs(omega[j + 1] - omega[j]) <
                                                           SAME_PARTIAL * std::abs(omega[j]);
                const bool left_ok =
                    j > 0 && std::abs(omega[j - 1] - omega[j]) < SAME_PARTIAL * std::abs(omega[j]);
                if (right_ok && right > here && (right >= left || !left_ok)) {
                    ++j;
                } else if (left_ok && left > here) {
                    --j;
                } else {
                    break;
                }
            }
            owner[k] = static_cast<uint16_t>(j);
        }
        const float pull = 1.0f - std::exp(-static_cast<float>(CONTROL) /
                                           (PULL_MS * 0.001f * static_cast<float>(rate)));
        const float r1 = ratio - 1.0f;
        for (size_t k = 0; k < bands; ++k) {
            const size_t p = owner[k];
            const float step = r1 * omega[p];
            field(&Block::step_r, k) = std::cos(step);
            field(&Block::step_i, k) = std::sin(step);
            float nr = field(&Block::rot_r, k), ni = field(&Block::rot_i, k);
            if (p != k) {
                nr += pull * (field(&Block::rot_r, p) - nr);
                ni += pull * (field(&Block::rot_i, p) - ni);
            }
            const float mag = std::sqrt(nr * nr + ni * ni);
            if (mag > 1e-6f) {
                nr /= mag;
                ni /= mag;
            } else {
                nr = field(&Block::rot_r, p);
                ni = field(&Block::rot_i, p);
            }
            pending_r[k] = nr; // written back below, after every band has read its peak's
            pending_i[k] = ni;
        }
        for (size_t k = 0; k < bands; ++k) {
            field(&Block::rot_r, k) = pending_r[k];
            field(&Block::rot_i, k) = pending_i[k];
        }
    }

    double rate = 48000.0;
    float ratio = 1.0f;
    size_t bands = 0;
    size_t target_delay = 0;
    size_t bass_latency = 0;
    int counter = 0;
    vector<Block> blocks;
    vector<float> center, smoothed_energy, omega;
    vector<float> wr, wi, pending_r, pending_i;
    vector<std::complex<double>> base_w;
    vector<uint16_t> owner;
    vector<uint32_t> delay;
    vector<float> line_r, line_i; // [position × bands + band]
    size_t line_len = 1, line_pos = 0;
};

} // namespace namx
