#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include <chowdsp_wdf/chowdsp_wdf.h>

#include "Common.h"
#include "Oversampler4x.h"

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
// Oversampling: Oversampler4x (latency_samples()). Nothing here allocates.
class TubeScreamerClipper
{
public:
    // Total delay of the oversampling filters, in base-rate samples.
    static constexpr double latency_samples() {
        return Oversampler4x::latency_samples();
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
        oversampler.reset();
    }

    // Drive 0–1 → the 500 kΩ log pot (≈ quadratic) after 51 kΩ.
    void set_drive(float drive) {
        const float d = std::clamp(drive, 0.0f, 1.0f);
        source.setResistanceValue(51.0e3f + 500.0e3f * d * d);
    }

    // Volts in, volts out (the caller scales digital full scale to volts).
    float process(float vin) {
        // The circuit at 4× the rate.
        std::array<float, 4> x4{};
        oversampler.up(vin, x4);
        for (float& v : x4) {
            v = circuit(v);
        }
        return oversampler.down(x4);
    }

private:
    static constexpr double HP_HZ = 1.0 / (2.0 * std::numbers::pi * 4.7e3 * 47.0e-9);
    static constexpr float R_LEG = 4.7e3f;
    // 1N914: saturation current and thermal voltage × ideality factor (Yeh 2007).
    static constexpr float DIODE_IS = 2.52e-9f;
    static constexpr float DIODE_VT = 25.85e-3f * 1.752f;

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

    double os_rate = 192000.0;
    float hp_b = 1.0f, hp_a = 0.0f, hp_x1 = 0.0f, hp_y1 = 0.0f;
    Oversampler4x oversampler;

    // The WDF tree (members in construction order: children before their adaptors).
    chowdsp::wdft::ResistiveCurrentSourceT<float> source{51.0e3f};
    chowdsp::wdft::CapacitorT<float> c_fb{51.0e-12f};
    chowdsp::wdft::WDFParallelT<float, decltype(source), decltype(c_fb)> parallel{source, c_fb};
    chowdsp::wdft::DiodePairT<float, decltype(parallel)> diodes{parallel, DIODE_IS, DIODE_VT};
};

} // namespace namx
