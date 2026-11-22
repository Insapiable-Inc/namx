#pragma once

#include <array>
#include <cmath>
#include <numbers>

namespace namx
{

// 4× oversampling for one channel: two 2× stages each way, linear-phase half-band FIRs.
// up() turns one sample into four, down() four into one; what runs in between is the
// caller's. Nothing here allocates.
class Oversampler4x
{
public:
    // Total delay of the filters, up and down, in base-rate samples.
    static constexpr double latency_samples() {
        // Each half-band FIR delays (TAPS − 1) / 2 samples at its own rate; up and down,
        // at 2× and 4×: 2 · (h/2 + h/4) with h = (TAPS − 1) / 2.
        return 2.0 * ((TAPS - 1) / 2.0 / 2.0 + (TAPS - 1) / 2.0 / 4.0);
    }

    void reset() {
        for (auto* s : {&up1, &up2, &down1, &down2}) {
            s->fill(0.0f);
        }
        pos1 = pos2 = pos3 = pos4 = 0;
    }

    // 1× → 2× → 4×: zero-stuff and filter twice (gain 2 per stage restores the level).
    void up(float x, std::array<float, 4>& out) {
        float x2[2];
        x2[0] = 2.0f * fir(up1, pos1, x);
        x2[1] = 2.0f * fir(up1, pos1, 0.0f);
        for (size_t i = 0; i < 2; ++i) {
            out[2 * i] = 2.0f * fir(up2, pos2, x2[i]);
            out[2 * i + 1] = 2.0f * fir(up2, pos2, 0.0f);
        }
    }

    // 4× → 2× → 1×: filter, keep every other sample.
    float down(const std::array<float, 4>& in) {
        float y2[2];
        for (size_t i = 0; i < 2; ++i) {
            const float a = fir(down2, pos3, in[2 * i]);
            fir(down2, pos3, in[2 * i + 1]);
            y2[i] = a;
        }
        const float y = fir(down1, pos4, y2[0]);
        fir(down1, pos4, y2[1]);
        return y;
    }

private:
    static constexpr int TAPS = 31; // half-band FIR length (odd)

    // Half-band low-pass: windowed sinc at a quarter of the rate (Kaiser, β = 8).
    static constexpr std::array<float, TAPS> design() {
        std::array<float, TAPS> h{};
        constexpr int mid = (TAPS - 1) / 2;
        // Kaiser window via the series for I0, constexpr-friendly.
        auto i0 = [](double x) {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 30; ++k) {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        };
        constexpr double beta = 8.0;
        double total = 0.0;
        for (int n = 0; n < TAPS; ++n) {
            const int m = n - mid;
            double sinc = 0.5;
            if (m != 0) {
                // sin(π m / 2) / (π m): 0 for even m, ±1/(π m) for odd.
                sinc = (m % 2 == 0)
                           ? 0.0
                           : ((m % 4 == 1 || m % 4 == -3) ? 1.0 : -1.0) / (std::numbers::pi * m);
            }
            const double r = static_cast<double>(m) / mid;
            const double w = i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0(beta);
            h[static_cast<size_t>(n)] = static_cast<float>(sinc * w);
            total += sinc * w;
        }
        for (float& v : h) {
            v = static_cast<float>(v / total);
        }
        return h;
    }
    static inline const std::array<float, TAPS> HALF_BAND = design();

    // A direct-form FIR over a circular history (2 × TAPS so the window is contiguous).
    static float fir(std::array<float, 2 * TAPS>& hist, int& pos, float x) {
        hist[static_cast<size_t>(pos)] = x;
        hist[static_cast<size_t>(pos + TAPS)] = x;
        const float* window = &hist[static_cast<size_t>(pos + 1)]; // oldest → newest
        float acc = 0.0f;
        for (int k = 0; k < TAPS; ++k) {
            acc += HALF_BAND[static_cast<size_t>(TAPS - 1 - k)] * window[k];
        }
        pos = pos + 1 == TAPS ? 0 : pos + 1;
        return acc;
    }

    std::array<float, 2 * TAPS> up1{}, up2{}, down1{}, down2{};
    int pos1 = 0, pos2 = 0, pos3 = 0, pos4 = 0;
};

} // namespace namx
