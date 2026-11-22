#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include <signalsmith-linear/fft.h>

namespace namx::rv6::shimmer
{

// Overlap-save at three partition sizes. The larger tiers start two partitions into
// the IR: one partition to collect input, the next to distribute its products across
// 64-sample callbacks. All outputs sharing an input also share its forward transforms.
constexpr size_t BLOCK = 64;
using Block = std::array<float, BLOCK>;
constexpr std::array<size_t, 3> SIZES{BLOCK, 512, 2048};
constexpr std::array<size_t, 3> START{0, 2 * SIZES[1], 2 * SIZES[2]};
using Counts = std::array<size_t, SIZES.size()>;
using Buffer = std::vector<float>;

struct Transforms
{
    using FFT = signalsmith::linear::RealFFT<float>;
    std::array<FFT, 3> fft{FFT{2 * SIZES[0]}, FFT{2 * SIZES[1]}, FFT{2 * SIZES[2]}};
    std::array<float, 2 * SIZES.back()> time{};
};

struct Kernel
{
    std::array<Buffer, 3> spectra;
    Counts count{};

    void design(std::span<const float> taps, Transforms& transform) {
        for (size_t t = 0; t < SIZES.size() && START[t] < taps.size(); ++t) {
            const size_t size = SIZES[t];
            const size_t end =
                t + 1 < START.size() ? std::min(START[t + 1], taps.size()) : taps.size();
            count[t] = (end - START[t] + size - 1) / size;
            spectra[t].resize(count[t] * 2 * size);
            for (size_t p = 0; p < count[t]; ++p) {
                const size_t from = START[t] + p * size;
                const size_t length = std::min(size, end - from);
                std::copy_n(taps.data() + from, length, transform.time.data());
                std::fill(transform.time.begin() + static_cast<ptrdiff_t>(length),
                          transform.time.begin() + static_cast<ptrdiff_t>(2 * size), 0.0f);
                float* h = spectra[t].data() + p * 2 * size;
                transform.fft[t].fft(transform.time.data(), h, h + size);
                for (size_t k = 0; k < 2 * size; ++k) {
                    h[k] /= static_cast<float>(2 * size);
                }
            }
        }
    }
};

inline void include(Counts& capacity, const Kernel& kernel) noexcept {
    for (size_t t = 0; t < capacity.size(); ++t) {
        capacity[t] = std::max(capacity[t], kernel.count[t]);
    }
}

struct InputHistory
{
    struct Tier
    {
        Buffer frame, spectra;
        size_t capacity = 0, newest = 0, valid = 0, offset = 0;
    };
    std::array<Tier, 3> tiers;

    void prepare(const Counts& counts) {
        for (size_t t = 0; t < tiers.size(); ++t) {
            auto& h = tiers[t];
            if (counts[t] == 0) {
                continue;
            }
            h.capacity = counts[t] + 1;
            h.frame.resize(2 * SIZES[t]);
            h.spectra.resize(h.capacity * 2 * SIZES[t]);
        }
    }

    void reset(size_t stagger = 0) noexcept {
        for (size_t t = 0; t < tiers.size(); ++t) {
            auto& h = tiers[t];
            h.newest = h.valid = 0;
            h.offset = stagger * BLOCK % SIZES[t];
            std::fill(h.frame.begin(), h.frame.end(), 0.0f);
        }
    }

    void push(const Block& input, Transforms& transform) noexcept {
        for (size_t t = 0; t < tiers.size(); ++t) {
            auto& h = tiers[t];
            if (h.capacity == 0) {
                continue;
            }
            const size_t n = SIZES[t];
            std::copy(input.begin(), input.end(), h.frame.data() + n + h.offset);
            h.offset += BLOCK;
            if (h.offset == n) {
                h.offset = 0;
                h.newest = (h.newest + 1 == h.capacity) ? 0 : h.newest + 1;
                h.valid = std::min(h.valid + 1, h.capacity);
                float* x = h.spectra.data() + h.newest * 2 * n;
                transform.fft[t].fft(h.frame.data(), x, x + n);
                std::copy_n(h.frame.data() + n, n, h.frame.data());
            }
        }
    }
};

class FilterOutput
{
public:
    void prepare(const Kernel& kernel) {
        for (size_t t = 0; t < SIZES.size(); ++t) {
            if (kernel.count[t] != 0) {
                sums[t].resize(2 * SIZES[t]);
                if (t != 0) {
                    next[t].resize(SIZES[t]);
                }
            }
        }
    }

    void reset() noexcept {
        for (size_t t = 1; t < SIZES.size(); ++t) {
            std::fill(sums[t].begin(), sums[t].end(), 0.0f);
            std::fill(next[t].begin(), next[t].end(), 0.0f);
        }
    }

    void run(const InputHistory& input, const Kernel& kernel, Transforms& transform,
             Block& out) noexcept {
        out.fill(0.0f);
        for (size_t t = 0; t < SIZES.size() && kernel.count[t] != 0; ++t) {
            const size_t n = SIZES[t];
            const auto& h = input.tiers[t];
            const size_t offset = h.offset == 0 ? n - BLOCK : h.offset - BLOCK;
            if (t != 0) {
                for (size_t k = 0; k < BLOCK; ++k) {
                    out[k] += next[t][offset + k];
                }
            }
            float* acc = sums[t].data();
            if (offset == 0) {
                std::fill_n(acc, 2 * n, 0.0f);
            }
            // The smallest tier uses the block just received. The larger tiers work
            // on the preceding frame, including on the tick that writes the new one.
            const size_t lag = (t != 0 && h.offset == 0) ? 1 : 0;
            const size_t first = kernel.count[t] * offset / n;
            const size_t end =
                std::min(kernel.count[t] * (offset + BLOCK) / n, h.valid > lag ? h.valid - lag : 0);
            if (first < end) {
                size_t slot = (h.newest + h.capacity - lag - first) % h.capacity;
                for (size_t p = first; p < end; ++p) {
                    multiply_add(acc, h.spectra.data() + slot * 2 * n,
                                 kernel.spectra[t].data() + p * 2 * n, n);
                    slot = slot == 0 ? h.capacity - 1 : slot - 1;
                }
            }
            if (h.offset == 0) {
                transform.fft[t].ifft(acc, acc + n, transform.time.data());
                if (t == 0) {
                    std::copy_n(transform.time.data() + n, BLOCK, out.data());
                } else {
                    std::copy_n(transform.time.data() + n, n, next[t].data());
                }
            }
        }
    }

private:
    static void multiply_add(float* a, const float* x, const float* h, size_t n) noexcept {
        // Signalsmith packs DC in real[0] and Nyquist in imaginary[0].
        a[0] += x[0] * h[0];
        a[n] += x[n] * h[n];
        for (size_t k = 1; k < n; ++k) {
            a[k] += x[k] * h[k] - x[n + k] * h[n + k];
            a[n + k] += x[k] * h[n + k] + x[n + k] * h[k];
        }
    }

    std::array<Buffer, 3> sums, next;
};

} // namespace namx::rv6::shimmer
