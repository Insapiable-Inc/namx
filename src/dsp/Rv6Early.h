#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

#include <signalsmith-linear/fft.h>

namespace namx::rv6
{

// A measured early response: the pedal's wet over its first few hundred milliseconds
// (tools/capture/rv6_early.py), re the dry at Level and Time noon, at 48 kHz, as int16
// times `scale`, A and B; already windowed: zero before `offset`, a raised-cosine fade in
// over `fade_in` samples and out over the last `fade_out`.
struct EarlyKernel
{
    const int16_t* a = nullptr;
    const int16_t* b = nullptr;
    size_t length = 0; // stored taps, from `offset` on
    size_t offset = 0; // the first stored tap's delay, samples at 48 kHz
    size_t fade_in = 0;
    size_t fade_out = 0;
    float scale = 0.0f;
    float rate = 48000.0f;
    float decay_from_ms = 0.0f; // where Time's extra decay starts
    std::array<float, 5> gain_db{}; // its level at the five Time positions (noon 0), fitted
};

// The early response played by convolution: the kernel is the measured response (over
// the mode's gain, as the tank's output is before it) less the engine's own tank over the
// same window, at the engine's rate — tank plus kernel is the pedal there, and the tank
// alone after. Uniformly partitioned (blocks of BLOCK
// samples, FFT size 2 × BLOCK): each block's work produces the next block's output, on
// time because the kernel's first BLOCK taps are zero (its wet starts tens of
// milliseconds in), as in IRLoader's tail. Leading all-zero partitions are skipped. Time
// reshapes the kernel by partition: each one weighted by exp(−extra × (t − decay_from)),
// the faster (or slower) decay the tank gets at that Time, times the kernel's own gain
// there, so the measured start follows the knob as the pedal's does. Mono in, A and B
// out, one sample at a time; prepare() allocates, the rest doesn't.
class EarlyResponse
{
public:
    static constexpr size_t BLOCK = 256;

    // `tank_a`, `tank_b`: the tank's (equalized) impulse response at `sr`, at noon.
    void prepare(double sr, const EarlyKernel& kernel, float gain, const std::vector<float>& tank_a,
                 const std::vector<float>& tank_b) {
        rate = static_cast<float>(sr);
        std::vector<float> taps[2] = {resampled(kernel, kernel.a, sr),
                                      resampled(kernel, kernel.b, sr)};
        if (kernel.length > 0) {
            const double ratio = sr / static_cast<double>(kernel.rate);
            const double start = static_cast<double>(kernel.offset) * ratio;
            const double end = static_cast<double>(kernel.offset + kernel.length) * ratio;
            const double rise = std::max(1.0, static_cast<double>(kernel.fade_in) * ratio);
            const double fall = std::max(1.0, static_cast<double>(kernel.fade_out) * ratio);
            for (size_t ch = 0; ch < 2; ++ch) {
                const std::vector<float>& tank = ch == 0 ? tank_a : tank_b;
                for (size_t i = 0; i < taps[ch].size(); ++i) {
                    const auto n = static_cast<double>(i);
                    double w = 0.0;
                    if (n >= start && n < end) {
                        w = 1.0;
                        if (n < start + rise) {
                            w = 0.5 - 0.5 * std::cos(std::numbers::pi * (n - start) / rise);
                        } else if (n >= end - fall) {
                            w = 0.5 + 0.5 * std::cos(std::numbers::pi * (n - (end - fall)) / fall);
                        }
                    }
                    const float own = i < tank.size() ? tank[i] : 0.0f;
                    taps[ch][i] = taps[ch][i] / gain - static_cast<float>(w) * own;
                }
            }
        }
        size_t first = taps[0].size();
        for (const auto& t : taps) {
            const auto nonzero = std::find_if(t.begin(), t.end(), [](float v) {
                return v != 0.0f;
            });
            first = std::min(first, static_cast<size_t>(nonzero - t.begin()));
        }
        const size_t length = std::max(taps[0].size(), taps[1].size());
        // Partition j holds taps [BLOCK (1 + j), BLOCK (2 + j)); the first BLOCK must be zero.
        lead = first >= 2 * BLOCK ? first / BLOCK - 1 : 0;
        parts = length > BLOCK * (1 + lead) ? (length - BLOCK * (1 + lead) + BLOCK - 1) / BLOCK : 0;
        std::vector<float> padded(2 * BLOCK);
        for (size_t ch = 0; ch < 2; ++ch) {
            spectra[ch].assign(parts * BLOCK, Complex{});
            for (size_t p = 0; p < parts; ++p) {
                std::fill(padded.begin(), padded.end(), 0.0f);
                const size_t from = BLOCK * (1 + lead + p);
                for (size_t i = 0; i < BLOCK && from + i < taps[ch].size(); ++i) {
                    padded[i] = taps[ch][from + i];
                }
                fft.fft(padded.data(), &spectra[ch][p * BLOCK]);
            }
            out[ch].assign(BLOCK, 0.0f);
        }
        ring = parts + lead;
        fdl.assign(std::max<size_t>(ring, 1) * BLOCK, Complex{});
        frame.assign(2 * BLOCK, 0.0f);
        time.assign(2 * BLOCK, 0.0f);
        spectrum.assign(BLOCK, Complex{});
        weight.assign(parts, 1.0f);
        part_seconds.resize(parts);
        for (size_t p = 0; p < parts; ++p) {
            part_seconds[p] = static_cast<float>(BLOCK * (lead + p) + BLOCK * 3 / 2) / rate;
        }
        decay_from = kernel.decay_from_ms / 1000.0f;
        reset();
    }

    void reset() noexcept {
        std::fill(fdl.begin(), fdl.end(), Complex{});
        std::fill(frame.begin(), frame.end(), 0.0f);
        for (auto& o : out) {
            std::fill(o.begin(), o.end(), 0.0f);
        }
        newest = 0;
        pos = 0;
    }

    bool empty() const noexcept {
        return parts == 0;
    }

    // Where Time's extra decay starts on the kernel, in place of the kernel's own (fitting it).
    void set_decay_from(float ms) noexcept {
        decay_from = ms / 1000.0f;
    }

    // Time's decay re the kernel's own (noon): `extra` nepers per second faster (< 0:
    // slower), the whole kernel times `gain`.
    void set_time(float extra, float gain) noexcept {
        for (size_t p = 0; p < parts; ++p) {
            weight[p] = gain * std::exp(-extra * std::max(0.0f, part_seconds[p] - decay_from));
        }
    }

    void process(float x, float& a, float& b) noexcept {
        a = out[0][pos];
        b = out[1][pos];
        frame[BLOCK + pos] = x;
        if (++pos == BLOCK) {
            compute();
            pos = 0;
        }
    }

private:
    using Complex = std::complex<float>;

    // The kernel at the engine's rate: windowed-sinc interpolation (a discrete IR scales
    // by the rate ratio: its taps are the response's samples over the rate).
    static std::vector<float> resampled(const EarlyKernel& k, const int16_t* data, double sr) {
        const double ratio = sr / static_cast<double>(k.rate);
        const size_t total = k.offset + k.length;
        std::vector<float> in(total, 0.0f);
        for (size_t i = 0; i < k.length; ++i) {
            in[k.offset + i] = k.scale * static_cast<float>(data[i]);
        }
        if (std::abs(ratio - 1.0) < 1e-9) {
            return in;
        }
        constexpr int HALF = 32; // zero crossings each side
        constexpr int STEPS = 512; // table points per zero crossing
        // The Blackman-windowed sinc over |d| <= HALF, tabulated (read with linear
        // interpolation: a sine and two cosines per tap made 96 kHz take a quarter second).
        std::vector<float> table(HALF * STEPS + 2, 0.0f);
        for (size_t j = 0; j <= static_cast<size_t>(HALF * STEPS); ++j) {
            const double d = static_cast<double>(j) / STEPS;
            const double sinc =
                j == 0 ? 1.0 : std::sin(std::numbers::pi * d) / (std::numbers::pi * d);
            const double window = 0.42 + 0.5 * std::cos(std::numbers::pi * d / HALF) +
                                  0.08 * std::cos(2.0 * std::numbers::pi * d / HALF);
            table[j] = static_cast<float>(sinc * window);
        }
        const double cutoff = std::min(1.0, ratio); // re the input's Nyquist
        const auto length = static_cast<size_t>(std::ceil(static_cast<double>(total) * ratio)) +
                            static_cast<size_t>(2 * HALF);
        std::vector<float> result(length, 0.0f);
        const long reach = static_cast<long>(std::ceil(HALF / cutoff));
        for (size_t n = 0; n < length; ++n) {
            const double u = static_cast<double>(n) / ratio; // position in input samples
            const auto centre = static_cast<long>(std::floor(u));
            double sum = 0.0;
            for (long i = std::max(0L, centre - reach + 1);
                 i <= std::min(static_cast<long>(total) - 1, centre + reach); ++i) {
                const float x = in[static_cast<size_t>(i)];
                if (x == 0.0f) {
                    continue;
                }
                const double position = std::abs(u - static_cast<double>(i)) * cutoff * STEPS;
                const auto j = static_cast<size_t>(position);
                if (j >= static_cast<size_t>(HALF * STEPS)) {
                    continue;
                }
                const double f = position - static_cast<double>(j);
                const double kernel = table[j] + f * (table[j + 1] - table[j]);
                sum += static_cast<double>(x) * kernel * cutoff;
            }
            result[n] = static_cast<float>(sum / ratio);
        }
        return result;
    }

    // One block in: its spectrum joins the delay line; every partition's product, weighted,
    // is summed per output; the inverse transforms' second halves are the next block.
    void compute() noexcept {
        if (parts == 0) {
            std::copy(frame.begin() + BLOCK, frame.end(), frame.begin());
            return;
        }
        newest = newest + 1 == ring ? 0 : newest + 1;
        fft.fft(frame.data(), &fdl[newest * BLOCK]);
        std::copy(frame.begin() + BLOCK, frame.end(), frame.begin());
        const float scale = 1.0f / static_cast<float>(2 * BLOCK);
        for (size_t ch = 0; ch < 2; ++ch) {
            std::fill(spectrum.begin(), spectrum.end(), Complex{});
            auto* const acc = reinterpret_cast<float*>(spectrum.data());
            size_t index = (newest + ring - lead % ring) % ring;
            for (size_t p = 0; p < parts; ++p) {
                const auto* const x = reinterpret_cast<const float*>(&fdl[index * BLOCK]);
                const auto* const h = reinterpret_cast<const float*>(&spectra[ch][p * BLOCK]);
                const float w = weight[p];
                // Bin 0 packs DC (real) and Nyquist (imaginary): two real products.
                acc[0] += w * x[0] * h[0];
                acc[1] += w * x[1] * h[1];
                for (size_t k = 1; k < BLOCK; ++k) {
                    const float xr = x[2 * k], xi = x[2 * k + 1];
                    const float hr = w * h[2 * k], hi = w * h[2 * k + 1];
                    acc[2 * k] += xr * hr - xi * hi;
                    acc[2 * k + 1] += xr * hi + xi * hr;
                }
                index = index == 0 ? ring - 1 : index - 1;
            }
            fft.ifft(spectrum.data(), time.data());
            for (size_t i = 0; i < BLOCK; ++i) {
                out[ch][i] = time[BLOCK + i] * scale;
            }
        }
    }

    signalsmith::linear::RealFFT<float> fft{2 * BLOCK};
    std::vector<Complex> spectra[2]; // parts × BLOCK each
    std::vector<Complex> fdl; // ring × BLOCK: the input's block spectra, newest at `newest`
    std::vector<float> frame, time, out[2], weight, part_seconds;
    std::vector<Complex> spectrum;
    size_t parts = 0, lead = 0, ring = 1, newest = 0, pos = 0;
    float rate = 48000.0f, decay_from = 0.0f;
};

} // namespace namx::rv6
