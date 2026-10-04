#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include <chowdsp_wdf/chowdsp_wdf.h>

#include "Common.h"

namespace namx
{

// The Tube Screamer's clipping stage as a wave digital filter (chowdsp_wdf, BSD-3),
// run at 4× the sample rate. Mono, one per channel.
//
// The TS808/TS9 clipper is a non-inverting op-amp stage: the inverting input follows the
// input voltage Vin, so the current through the 4.7 kΩ + 47 nF leg to ground,
// I = Vin · sC/(1 + sRC) (a 720 Hz high-pass over 4.7 kΩ), all flows through the
// feedback network — 51 kΩ + the 500 kΩ Drive pot, in parallel with 51 pF and two
// antiparallel 1N914 diodes — and the output is Vin + the voltage across it. So only
// what's above ~720 Hz gets the gain (21–41 dB) and the diodes' soft clipping, and the
// clean signal is added back: the TS mid hump and tight lows. The WDF: that current as
// a current source with the feedback resistance in parallel, the 51 pF beside it, the
// diode pair at the root (solved in closed form, Wright omega).
//
// Oversampling: two 2× stages each way, linear-phase half-band FIRs (latency_samples()).
// Nothing here allocates.
class TubeScreamerClipper
{
public:
    // Total delay of the oversampling filters, in base-rate samples.
    static constexpr double latency_samples() {
        // Each half-band FIR delays (TAPS − 1) / 2 samples at its own rate; up and down,
        // at 2× and 4×: 2 · (h/2 + h/4) with h = (TAPS − 1) / 2.
        return 2.0 * ((TAPS - 1) / 2.0 / 2.0 + (TAPS - 1) / 2.0 / 4.0);
    }

    void prepare(double sr) {
        os_rate = 4.0 * sr;
        c_fb.prepare(static_cast<float>(os_rate));
        // The 4.7 kΩ / 47 nF leg's high-pass, bilinear at the oversampled rate.
        const double k = std::tan(std::numbers::pi * HP_HZ / os_rate);
        hp_b = static_cast<float>(1.0 / (1.0 + k));
        hp_a = static_cast<float>((1.0 - k) / (1.0 + k));
        reset();
    }

    void reset() {
        c_fb.reset();
        hp_x1 = hp_y1 = 0.0f;
        for (auto* s : {&up1, &up2, &down1, &down2}) {
            s->fill(0.0f);
        }
        pos1 = pos2 = pos3 = pos4 = 0;
    }

    // Drive 0–1 → the 500 kΩ log pot (≈ quadratic) after 51 kΩ.
    void set_drive(float drive) {
        const float d = std::clamp(drive, 0.0f, 1.0f);
        source.setResistanceValue(51.0e3f + 500.0e3f * d * d);
    }

    // Volts in, volts out (the caller scales digital full scale to volts).
    float process(float vin) {
        // 1× → 2× → 4×: zero-stuff and filter twice (gain 2 per stage restores the level).
        float x2[2], x4[4];
        x2[0] = 2.0f * fir(up1, pos1, vin);
        x2[1] = 2.0f * fir(up1, pos1, 0.0f);
        for (int i = 0; i < 2; ++i) {
            x4[2 * i] = 2.0f * fir(up2, pos2, x2[i]);
            x4[2 * i + 1] = 2.0f * fir(up2, pos2, 0.0f);
        }
        // The circuit at 4×, then 4× → 2× → 1× (filter, keep every other sample).
        float y2[2];
        for (int i = 0; i < 2; ++i) {
            const float a = fir(down2, pos3, circuit(x4[2 * i]));
            fir(down2, pos3, circuit(x4[2 * i + 1]));
            y2[i] = a;
        }
        const float y = fir(down1, pos4, y2[0]);
        fir(down1, pos4, y2[1]);
        return y;
    }

private:
    static constexpr double HP_HZ = 1.0 / (2.0 * std::numbers::pi * 4.7e3 * 47.0e-9);
    static constexpr float R_LEG = 4.7e3f;
    // 1N914: saturation current and thermal voltage × ideality factor (Yeh 2007).
    static constexpr float DIODE_IS = 2.52e-9f;
    static constexpr float DIODE_VT = 25.85e-3f * 1.752f;
    static constexpr int TAPS = 31; // half-band FIR length (odd)

    // One step of the oversampled circuit.
    float circuit(float vin) {
        // Current into the feedback network: the 720 Hz high-pass of Vin over 4.7 kΩ.
        const float hp = hp_b * (vin - hp_x1) + hp_a * hp_y1;
        hp_x1 = vin;
        hp_y1 = hp;
        source.setCurrent(hp / R_LEG);
        diodes.incident(parallel.reflected());
        parallel.incident(diodes.reflected());
        return vin + chowdsp::wdft::voltage<float>(c_fb);
    }

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

    double os_rate = 192000.0;
    float hp_b = 1.0f, hp_a = 0.0f, hp_x1 = 0.0f, hp_y1 = 0.0f;
    std::array<float, 2 * TAPS> up1{}, up2{}, down1{}, down2{};
    int pos1 = 0, pos2 = 0, pos3 = 0, pos4 = 0;

    // The WDF tree (members in construction order: children before their adaptors).
    chowdsp::wdft::ResistiveCurrentSourceT<float> source{51.0e3f};
    chowdsp::wdft::CapacitorT<float> c_fb{51.0e-12f};
    chowdsp::wdft::WDFParallelT<float, decltype(source), decltype(c_fb)> parallel{source, c_fb};
    chowdsp::wdft::DiodePairT<float, decltype(parallel)> diodes{parallel, DIODE_IS, DIODE_VT};
};

} // namespace namx
