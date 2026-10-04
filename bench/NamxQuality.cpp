// Effect quality numbers for BENCHMARKS.md (Phase 27): the yardstick Phase 22 needs
// before replacing any DSP. Each processor gets fixed test signals (sines, impulses,
// bursts — nothing random) and the output is measured: aliasing, pitch error, decay
// time, frequency response against an independent Audio EQ Cookbook implementation,
// limiter overshoot. Prints Markdown.
//
//   namx-quality [--model amp.nam]

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

#include <NAM/activations.h>

#include "Common.h"
#include "DelayProcessor.h"
#include "DriveProcessor.h"
#include "EQProcessor.h"
#include "NamProcessor.h"
#include "OutputLimiter.h"
#include "ReverbProcessor.h"
#include "TransposeProcessor.h"

namespace namx
{

namespace
{

constexpr double SR = 48000.0;
constexpr size_t BLOCK = 128;
constexpr double PI = std::numbers::pi;

double db(double ratio) {
    return 10.0 * std::log10(std::max(ratio, 1e-30));
}

// Runs `n` samples of `input(t)` through `process(l, r, frames)` in blocks; mono out (L).
template <typename In, typename Proc>
vector<float> render(size_t n, In input, Proc process) {
    vector<float> out(n), l(BLOCK), r(BLOCK);
    for (size_t t = 0; t < n; t += BLOCK) {
        const size_t frames = std::min(BLOCK, n - t);
        for (size_t i = 0; i < frames; ++i) {
            l[i] = r[i] = static_cast<float>(input(t + i));
        }
        process(l.data(), r.data(), frames);
        std::copy(l.begin(), l.begin() + static_cast<long>(frames),
                  out.begin() + static_cast<long>(t));
    }
    return out;
}

// Power spectrum of the last `n` (power of two) samples, Blackman-Harris windowed.
vector<double> power_spectrum(const vector<float>& x, size_t n) {
    vector<std::complex<double>> a(n);
    const size_t start = x.size() - n;
    for (size_t i = 0; i < n; ++i) {
        const double w = 2.0 * PI * static_cast<double>(i) / static_cast<double>(n - 1);
        const double bh =
            0.35875 - 0.48829 * std::cos(w) + 0.14128 * std::cos(2 * w) - 0.01168 * std::cos(3 * w);
        a[i] = x[start + i] * bh;
    }
    // Iterative radix-2 FFT.
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(a[i], a[j]);
        }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const std::complex<double> wl = std::polar(1.0, -2.0 * PI / static_cast<double>(len));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w = 1.0;
            for (size_t k = 0; k < len / 2; ++k, w *= wl) {
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
    vector<double> p(n / 2);
    for (size_t k = 0; k < n / 2; ++k) {
        p[k] = std::norm(a[k]);
    }
    return p;
}

constexpr size_t FFT_N = 16384;
constexpr int LOBE = 5; // Blackman-Harris main lobe half-width, bins

// A frequency on an exact FFT bin (no leakage between harmonics), on an odd bin so it
// never divides the sample rate: otherwise every alias folds back exactly onto a harmonic
// and the off-harmonic measurement can't see it.
double on_bin(double hz) {
    double bin = std::round(hz * FFT_N / SR);
    if (std::fmod(bin, 2.0) == 0.0) {
        bin += 1.0;
    }
    return bin * SR / FFT_N;
}

// Energy within the main lobe around `hz`.
double band_energy(const vector<double>& p, double hz) {
    const long k = std::lround(hz * FFT_N / SR);
    double e = 0.0;
    for (long i = std::max(0L, k - LOBE);
         i <= std::min<long>(static_cast<long>(p.size()) - 1, k + LOBE); ++i) {
        e += p[static_cast<size_t>(i)];
    }
    return e;
}

double total_energy(const vector<double>& p) {
    double e = 0.0;
    for (size_t k = static_cast<size_t>(LOBE) + 1; k < p.size(); ++k) { // skip DC
        e += p[k];
    }
    return e;
}

// Energy off the harmonic series of `f0` (aliases, artifacts), relative to the total, dB.
double off_harmonic_db(const vector<float>& y, double f0) {
    const vector<double> p = power_spectrum(y, FFT_N);
    double harmonic = 0.0;
    for (double h = f0; h < SR / 2 - 200.0; h += f0) {
        harmonic += band_energy(p, h);
    }
    const double total = total_energy(p);
    return db((total - harmonic) / total);
}

// ---- Drive and amp aliasing ----------------------------------------------------------

void aliasing(const string& model) {
    std::printf("## Aliasing\n\nA sine at −6 dBFS, 2 s; energy *off* the harmonic series of the "
                "input (aliases folded back from above Nyquist, as inharmonic tones) relative "
                "to the whole output. 48 kHz, no oversampling. Lower is cleaner.\n\n");
    std::printf("| Input | Drive 100 %% | Drive 30 %% (default) |%s\n",
                model.empty() ? "" : " Amp (model) |");
    std::printf("|---|---|---|%s\n", model.empty() ? "" : "---|");
    for (const double hz : {1000.0, 3000.0, 5000.0, 7000.0}) {
        const double f = on_bin(hz);
        auto sine = [f](size_t t) {
            return 0.5 * std::sin(2 * PI * f * static_cast<double>(t) / SR);
        };
        auto drive_at = [&](float pct) {
            DriveProcessor d;
            d.set_sample_rate(SR);
            d.set_enabled(true);
            d.set_drive_pct(pct);
            return off_harmonic_db(render(size_t(2 * SR), sine,
                                          [&](float* l, float* r, size_t n) {
                                              d.process(l, r, n, false);
                                          }),
                                   f);
        };
        std::printf("| %.0f Hz | %.1f dB | %.1f dB |", hz, drive_at(100.0f), drive_at(30.0f));
        if (!model.empty()) {
            NamProcessor amp;
            amp.set_sample_rate(SR);
            amp.prepare_buffer(BLOCK);
            amp.set_enabled(true);
            amp.load_model(model);
            amp.reclaim_retired(true);
            std::printf(" %.1f dB |", off_harmonic_db(render(size_t(2 * SR), sine,
                                                             [&](float* l, float* r, size_t n) {
                                                                 amp.process(l, r, n, false);
                                                             }),
                                                      f));
        }
        std::printf("\n");
    }
}

// ---- Transpose -------------------------------------------------------------------------

// Peak frequency with parabolic interpolation on the log spectrum.
double peak_hz(const vector<double>& p) {
    size_t k = 1;
    for (size_t i = 2; i + 1 < p.size(); ++i) {
        if (p[i] > p[k]) {
            k = i;
        }
    }
    const double a = std::log(p[k - 1] + 1e-30), b = std::log(p[k] + 1e-30),
                 c = std::log(p[k + 1] + 1e-30);
    const double offset = 0.5 * (a - c) / (a - 2 * b + c);
    return (static_cast<double>(k) + offset) * SR / FFT_N;
}

void transpose() {
    std::printf("\n## Transpose\n\nA 220 Hz sine at −10 dBFS, mix 100 %%. Pitch error of the "
                "spectral peak, artifact level (energy off the target note's harmonics), and "
                "latency (input start → the output's 5 ms RMS envelope reaching half its steady "
                "level; range over eight onset times).\n\n");
    std::printf("| Shift | Pitch error | Artifacts | Latency |\n|---|---|---|---|\n");
    for (const float st : {-12.0f, -5.0f, -2.0f, 2.0f, 5.0f, 12.0f}) {
        auto make = [st] {
            auto t = make_unique<TransposeProcessor>();
            t->set_sample_rate(SR);
            t->set_enabled(true);
            t->set_semitones(st);
            return t;
        };
        auto tp = make();
        const vector<float> y = render(
            size_t(3 * SR),
            [](size_t t) {
                return 0.3 * std::sin(2 * PI * 220.0 * static_cast<double>(t) / SR);
            },
            [&](float* l, float* r, size_t n) {
                tp->process(l, r, n, false);
            });
        const double target = 220.0 * std::pow(2.0, st / 12.0);
        const double cents = 1200.0 * std::log2(peak_hz(power_spectrum(y, FFT_N)) / target);
        const double artifacts = off_harmonic_db(y, target);

        // Latency: silence, then the sine. The shifter's taps sweep a 50 ms window, so the
        // delay depends on where they are when the note starts: eight onsets 6.25 ms apart.
        double fastest = 1e9, slowest = 0.0;
        for (int k = 0; k < 8; ++k) {
            auto tl = make();
            const size_t onset = size_t(SR) + static_cast<size_t>(k) * 300;
            const vector<float> z = render(
                size_t(2 * SR),
                [onset](size_t t) {
                    return t < onset ? 0.0
                                     : 0.3 * std::sin(2 * PI * 220.0 * static_cast<double>(t) / SR);
                },
                [&](float* l, float* r, size_t n) {
                    tl->process(l, r, n, false);
                });
            // 5 ms RMS envelope: the crossfading taps make single samples dip.
            const size_t win = size_t(0.005 * SR);
            auto env = [&](size_t end) {
                double sum = 0.0;
                for (size_t i = end - win; i < end; ++i) {
                    sum += static_cast<double>(z[i]) * z[i];
                }
                return std::sqrt(sum / static_cast<double>(win));
            };
            const double steady = env(z.size());
            size_t reach = onset + win;
            while (reach < z.size() && env(reach) < 0.5 * steady) {
                reach += 24; // 0.5 ms steps
            }
            const double ms = static_cast<double>(reach - win / 2 - onset) / SR * 1000.0;
            fastest = std::min(fastest, ms);
            slowest = std::max(slowest, ms);
        }
        std::printf("| %+.0f st | %+.1f cents | %.1f dB | %.0f–%.0f ms |\n", st, cents, artifacts,
                    fastest, slowest);
    }
}

// ---- Reverb ----------------------------------------------------------------------------

// Second-order Butterworth high- or low-pass, in place (for the reverb's mid-band RT60).
void butterworth(vector<float>& v, double hz, bool high) {
    const double w = 2.0 * M_PI * hz / SR, alpha = std::sin(w) / std::sqrt(2.0), c = std::cos(w);
    const double b0 = high ? (1.0 + c) / 2.0 : (1.0 - c) / 2.0;
    const double b1 = high ? -(1.0 + c) : 1.0 - c, b2 = b0;
    const double a0 = 1.0 + alpha, a1 = -2.0 * c, a2 = 1.0 - alpha;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    for (float& s : v) {
        const double y = (b0 * s + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
        x2 = x1;
        x1 = s;
        y2 = y1;
        y1 = y;
        s = static_cast<float>(y);
    }
}

// T20 (the −5 → −25 dB slope of the Schroeder curve, extrapolated to 60 dB); < 0 if the
// response never falls 25 dB.
double rt60_t20(const vector<float>& y) {
    vector<double> edc(y.size());
    double acc = 0.0;
    for (size_t i = y.size(); i-- > 0;) {
        acc += static_cast<double>(y[i]) * y[i];
        edc[i] = acc;
    }
    auto time_at = [&](double drop_db) {
        for (size_t i = 0; i < edc.size(); ++i) {
            if (db(edc[i] / edc[0]) <= drop_db) {
                return static_cast<double>(i) / SR;
            }
        }
        return -1.0;
    };
    const double t5 = time_at(-5.0), t25 = time_at(-25.0);
    return t25 < 0.0 ? -1.0 : 3.0 * (t25 - t5);
}

void reverb() {
    std::printf("\n## Reverb decay\n\nImpulse response at mix 100 %%, damping 50 %%, width 100 "
                "%%, band-passed to 500 Hz – 2 kHz (where decay is quoted and heard); RT60 from "
                "Schroeder backward integration (T20, extrapolated to 60 dB), against the time "
                "the Decay knob shows.\n\n| Decay | Shown | Plate | Hall | Room |\n"
                "|---|---|---|---|---|\n");
    for (const float decay : {0.0f, 25.0f, 50.0f, 75.0f, 100.0f}) {
        std::printf("| %.0f %% | %.2f s |", decay, ReverbProcessor::decay_seconds(decay));
        for (int type = 0; type < 3; ++type) {
            ReverbProcessor rv;
            rv.set_sample_rate(SR);
            rv.set_enabled(true);
            rv.set_stereo_mode(true);
            rv.set_mix_pct(100.0f);
            rv.set_type(type);
            rv.set_room_size_pct(decay);
            vector<float> y = render(
                size_t(20 * SR),
                [](size_t t) {
                    return t == 0 ? 1.0 : 0.0;
                },
                [&](float* l, float* r, size_t n) {
                    rv.process(l, r, n, false);
                });
            for (int pass = 0; pass < 2; ++pass) {
                butterworth(y, 500.0, true);
                butterworth(y, 2000.0, false);
            }
            const double t = rt60_t20(y);
            if (t < 0.0) {
                std::printf(" > 20 s |");
            } else {
                std::printf(" %.2f s |", t);
            }
        }
        std::printf("\n");
    }
}

// ---- Delay modulation ------------------------------------------------------------------

void delay_modulation() {
    std::printf("\n## Delay modulation\n\nA 1 kHz sine through the delay (250 ms, feedback 0, mix "
                "100 %%). The modulation's wanted sidebands sit within ±50 Hz of the note; the "
                "energy further away (zipper noise, interpolation artifacts) relative to the "
                "total:\n\n| Modulation | Off-note energy |\n|---|---|\n");
    for (const float mod : {0.0f, 30.0f, 100.0f}) {
        DelayProcessor d;
        d.set_sample_rate(SR);
        d.set_enabled(true);
        d.set_time(250.0f);
        d.set_feedback_pct(0.0f);
        d.set_mix_pct(100.0f);
        d.set_modulation(mod);
        const double f = on_bin(1000.0);
        const vector<float> y = render(
            size_t(3 * SR),
            [f](size_t t) {
                return 0.3 * std::sin(2 * PI * f * static_cast<double>(t) / SR);
            },
            [&](float* l, float* r, size_t n) {
                d.process(l, r, n, false);
            });
        const vector<double> p = power_spectrum(y, FFT_N);
        double near = 0.0;
        const long k0 = std::lround(f * FFT_N / SR), span = std::lround(50.0 * FFT_N / SR) + LOBE;
        for (long k = k0 - span; k <= k0 + span; ++k) {
            near += p[static_cast<size_t>(k)];
        }
        const double total = total_energy(p);
        std::printf("| %.0f %% | %.1f dB |\n", mod, db((total - near) / total));
    }
}

// ---- EQ against the Audio EQ Cookbook --------------------------------------------------

// |H| of one RBJ biquad at `hz` (independent of EQProcessor's code).
double rbj_mag(const char* type, double f0, double gain_db, double q, double hz) {
    const double a = std::pow(10.0, gain_db / 40.0), w0 = 2 * PI * f0 / SR;
    const double cw = std::cos(w0), sw = std::sin(w0), alpha = sw / (2 * q);
    double b0, b1, b2, a0, a1, a2;
    const string t = type;
    if (t == "peak") {
        b0 = 1 + alpha * a, b1 = -2 * cw, b2 = 1 - alpha * a;
        a0 = 1 + alpha / a, a1 = -2 * cw, a2 = 1 - alpha / a;
    } else if (t == "lowshelf" || t == "highshelf") {
        const double sq = 2 * std::sqrt(a) * (sw / 2 * std::sqrt(2.0)); // slope S = 1
        const double s = t == "lowshelf" ? 1.0 : -1.0;
        b0 = a * ((a + 1) - s * (a - 1) * cw + sq);
        b1 = s * 2 * a * ((a - 1) - s * (a + 1) * cw);
        b2 = a * ((a + 1) - s * (a - 1) * cw - sq);
        a0 = (a + 1) + s * (a - 1) * cw + sq;
        a1 = -s * 2 * ((a - 1) + s * (a + 1) * cw);
        a2 = (a + 1) + s * (a - 1) * cw - sq;
    } else { // "hp" / "lp", Butterworth
        const double s = t == "hp" ? 1.0 : -1.0;
        b0 = (1 + s * cw) / 2, b1 = -s * (1 + s * cw), b2 = (1 + s * cw) / 2;
        a0 = 1 + alpha, a1 = -2 * cw, a2 = 1 - alpha;
    }
    const std::complex<double> z = std::polar(1.0, -2 * PI * hz / SR);
    return std::abs((b0 + b1 * z + b2 * z * z) / (a0 + a1 * z + a2 * z * z));
}

void eq() {
    EQProcessor e;
    e.set_sample_rate(SR);
    e.set_enabled(true);
    e.set_gain(EQProcessor::Band::BASS, 6.0);
    e.set_gain(EQProcessor::Band::MIDDLE, -6.0);
    e.set_gain(EQProcessor::Band::HIGH_MID, 3.0);
    e.set_gain(EQProcessor::Band::TREBLE, -3.0);
    e.set_freq(EQProcessor::Band::LOW_CUT, 80.0);
    e.set_freq(EQProcessor::Band::HIGH_CUT, 8000.0);
    double worst = 0.0, worst_hz = 0.0;
    for (int i = 0; i < 40; ++i) {
        const double hz = 30.0 * std::pow(16000.0 / 30.0, i / 39.0);
        e.reset();
        const vector<float> y = render(
            size_t(SR),
            [hz](size_t t) {
                return 0.1 * std::sin(2 * PI * hz * static_cast<double>(t) / SR);
            },
            [&](float* l, float* r, size_t n) {
                e.process(l, r, n, false);
            });
        double peak = 0.0;
        for (size_t t = y.size() / 2; t < y.size(); ++t) {
            peak = std::max(peak, std::abs(static_cast<double>(y[t])));
        }
        const double expected =
            rbj_mag("hp", 80.0, 0, 0.7071, hz) * rbj_mag("lowshelf", 250.0, 6.0, 0, hz) *
            rbj_mag("peak", 1000.0, -6.0, 0.707, hz) * rbj_mag("peak", 3000.0, 3.0, 1.0, hz) *
            rbj_mag("highshelf", 4000.0, -3.0, 0, hz) * rbj_mag("lp", 8000.0, 0, 0.7071, hz);
        const double dev = std::abs(20 * std::log10(peak / 0.1 / expected));
        if (dev > worst) {
            worst = dev, worst_hz = hz;
        }
    }
    std::printf("\n## EQ response\n\nAll six bands engaged (Bass +6 dB @ 250 Hz, Middle −6 @ 1 k, "
                "High-Mid +3 @ 3 k, Treble −3 @ 4 k, Low Cut 80 Hz, High Cut 8 kHz), 40 sine "
                "probes 30 Hz–16 kHz against an independent Audio EQ Cookbook evaluation: "
                "**largest deviation %.3f dB** (at %.0f Hz).\n",
                worst, worst_hz);
}

// ---- Limiter ---------------------------------------------------------------------------

void limiter() {
    OutputLimiter lim;
    lim.set_sample_rate(SR);
    lim.set_enabled(true);
    double worst = 0.0;
    const char* worst_case = "";
    struct Case
    {
        const char* name;
        double (*signal)(size_t);
    };
    const Case cases[] = {
        {"sine burst at +6 dBFS",
         [](size_t t) {
             return t > 2400 ? 2.0 * std::sin(2 * PI * 440.0 * static_cast<double>(t) / SR) : 0.0;
         }},
        {"sine burst at +20 dBFS",
         [](size_t t) {
             return t > 2400 ? 10.0 * std::sin(2 * PI * 3000.0 * static_cast<double>(t) / SR) : 0.0;
         }},
        {"single-sample spikes of +12 dBFS",
         [](size_t t) {
             return t % 4800 == 100 ? 4.0 : 0.0;
         }},
        {"square at +12 dBFS",
         [](size_t t) {
             return t > 2400 ? ((t / 60) % 2 ? 4.0 : -4.0) : 0.0;
         }},
    };
    for (const Case& c : cases) {
        const vector<float> y = render(size_t(SR), c.signal, [&](float* l, float* r, size_t n) {
            lim.process(l, r, n);
        });
        double peak = 0.0;
        for (float v : y) {
            peak = std::max(peak, std::abs(static_cast<double>(v)));
        }
        const double over = 20 * std::log10(peak / 0.96605);
        if (over > worst || worst_case[0] == 0) {
            worst = over, worst_case = c.name;
        }
    }
    std::printf("\n## Limiter\n\nCeiling −0.3 dBFS. Worst overshoot over four brutal inputs (sine "
                "bursts at +6 and +20 dBFS, +12 dBFS single-sample spikes, a +12 dBFS "
                "square): **%+.3f dB** relative to the ceiling (%s). Sample peaks; inter-sample "
                "peaks aren't measured.\n",
                worst, worst_case);
}

} // namespace

int run(int argc, char** argv) {
    string model;
    for (int i = 1; i + 1 < argc; ++i) {
        if (string(argv[i]) == "--model") {
            model = argv[++i];
        }
    }
    nam::activations::Activation::enable_fast_tanh(); // as in the app
    aliasing(model);
    transpose();
    reverb();
    delay_modulation();
    eq();
    limiter();
    return 0;
}

} // namespace namx

int main(int argc, char** argv) {
    return namx::run(argc, argv);
}
