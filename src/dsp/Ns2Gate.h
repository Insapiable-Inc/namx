#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "BiquadChain.h"
#include "Common.h"

namespace namx
{

// The Boss NS-2 Noise Suppressor (Mode: Reduction), fitted to captures of a real one. Its
// control circuit as a model, fitted to the gain read directly (2026-11-21: the signal into
// INPUT, a steady probe into RETURN, so the output is the probe times the gain at every
// instant; captures/boss/ns-2/README.md → The gain read directly, tools/capture/ns2/). Not a
// hard gate: from closed to open the gain follows the input's level, linearly in amplitude,
// over about 7 dB; a louder input holds it open for a while after.
//
// In volts above the pedal's 4.5 V reference:
//
//   u   the input → two high-passes → the Threshold amplifier (gain A; its bandwidth falls as
//       A rises)
//   w   |u|, clamped (the amplifier's swing), smoothed over 0.1 ms: the rectifier's output
//   v   the hold capacitor (4.7 µF): charged at once through a diode from w less a diode's
//       drop, discharged through the Decay pot back into the rectifier's output (so only
//       while v > w: a signal that carries on slows the closing)
//   g   the gain: v / SPAN, from FLOOR to 1 (the VCA is linear in its control voltage, which
//       the pedal clamps at fully open)
//
// A loud note charges v well past SPAN: the gain holds at 1 until v has fallen back to it
// (the longer the slower Decay is), then falls with v. A quiet one settles where the diode's
// current into the capacitor balances the pot's out of it: the gain creeps for a while after
// each change of level, as the pedal's does.
namespace ns2
{

// A hard-played guitar's peak, as for the other pedal models (Dd8Delay.h, Cs3Compressor.h):
// an assumption — the pedal sat before the amp.
constexpr float REFERENCE_DBFS = -6.0f;

// Open, the pedal is 0.6 dB over its bypass at 1 kHz.
constexpr float OPEN_GAIN_DB = 0.6f;

// The open path's top end, re its 1 kHz gain: flat to 3 kHz, −1.3 dB at 8 kHz, −2.4 at
// 12 kHz, −3.1 at 20 kHz (within 0.09 dB; tools/capture/fit_voicings.py, 2026-10-29). A low
// shelf that BiquadChain scales to unity at 1 kHz: in effect a −3.7 dB high shelf.
constexpr size_t TONE_SECTIONS = 1;
constexpr FilterChain<TONE_SECTIONS> TONE{{
    {FilterKind::LOW_SHELF, 8168.4f, 0.623f, 3.69f},
}};

// The two knobs keep the scales NAM-X gave them with the first model (2026-10-27), which
// place the pedal's positions: Threshold as a level in dB re the reference (plus OPEN_DB:
// where that model's switch opened), Decay in ms, at 7:00, 9:00, 12:00, 3:00, 5:00.
constexpr float OPEN_DB = 1.29f;
constexpr std::array<float, 5> THRESHOLD_DB{-69.40f, -69.02f, -66.72f, -49.40f, -27.33f};
constexpr std::array<float, 5> DECAY_MS{24.6f, 63.5f, 93.0f, 145.1f, 460.3f};

// The Threshold knob in dBFS: the pedal's span.
constexpr float OPENS_LOW_DBFS = THRESHOLD_DB.front() + OPEN_DB + REFERENCE_DBFS;
constexpr float OPENS_HIGH_DBFS = THRESHOLD_DB.back() + OPEN_DB + REFERENCE_DBFS;

// The control circuit. One instance per channel; nothing here allocates.
class Control
{
public:
    void prepare(double new_rate) {
        rate = new_rate;
        step = 1.0 / rate;
        const double k = std::tan(std::numbers::pi * HIGH_PASS_HZ / rate);
        high_b = 1.0 / (1.0 + k);
        high_a = (1.0 - k) / (1.0 + k);
        smooth = 1.0 - std::exp(-2.0 * std::numbers::pi * RECTIFIER_HZ / rate);
        charge_keep = std::exp(-step * CHARGE_IS / (CAPACITOR * CHARGE_ETA));
        ramp = CHARGE_MAX_A * step / CAPACITOR;
        reset();
    }

    void reset() {
        high1_in = high1_out = high2_in = high2_out = 0.0;
        low = level = held = 0.0;
    }

    // The knobs: Threshold (dBFS, OPENS_LOW_DBFS … OPENS_HIGH_DBFS) and Decay (ms,
    // DECAY_MS's span). Between the measured positions the amplifier's gain and bandwidth
    // (12:00, 3:00, 5:00) and the pot's resistance are log-linear in the knob's value; under
    // 12:00 Threshold goes on along the 12:00 to 3:00 line.
    void set(float opens_at_dbfs, float decay_ms) {
        const double t =
            std::clamp(opens_at_dbfs, OPENS_LOW_DBFS, OPENS_HIGH_DBFS) - REFERENCE_DBFS - OPEN_DB;
        size_t i = t < THRESHOLD_DB[3] ? 0 : 1;
        const double along =
            (t - THRESHOLD_DB[i + 2]) / (THRESHOLD_DB[i + 3] - THRESHOLD_DB[i + 2]);
        gain = std::exp(LOG_GAIN[i] + along * (LOG_GAIN[i + 1] - LOG_GAIN[i]));
        const double band = std::exp(LOG_BAND[i] + along * (LOG_BAND[i + 1] - LOG_BAND[i]));
        low_keep = std::exp(-2.0 * std::numbers::pi * std::min(band, 0.45 * rate) / rate);
        const float ms = std::clamp(decay_ms, DECAY_MS.front(), DECAY_MS.back());
        i = 0;
        while (i + 2 < DECAY_MS.size() && ms > DECAY_MS[i + 1]) {
            ++i;
        }
        const double across = std::log(ms / DECAY_MS[i]) / std::log(DECAY_MS[i + 1] / DECAY_MS[i]);
        const double ohms = std::exp(LOG_OHMS[i] + across * (LOG_OHMS[i + 1] - LOG_OHMS[i]));
        discharge = step / (ohms * CAPACITOR);
    }

    // x: the input, 1.0 = the guitar's peak. Returns the gain, FLOOR … 1.
    float process(float x) {
        // audio thread
        const double a = high_b * (x - high1_in) + high_a * high1_out;
        high1_in = x;
        high1_out = a;
        const double b = high_b * (a - high2_in) + high_a * high2_out;
        high2_in = a;
        high2_out = b;
        low = b + low_keep * (low - b);
        const double u = gain * low;
        const double noise = gain * NOISE;
        level += smooth * (std::min(std::sqrt(u * u + noise * noise), CEILING) - level);
        // The diode into the capacitor, solved over the sample (dv/dt = IS/C · (exp((p − v)
        // / ETA) − 1) has a closed form), its driver's current limited.
        const double from = level - CHARGE_DROP;
        if (from > held) {
            const double z = 1.0 - (1.0 - std::exp((held - from) / CHARGE_ETA)) * charge_keep;
            held = std::min(from + CHARGE_ETA * std::log(z), held + ramp);
        }
        if (held > level) {
            held -= (held - level) * discharge;
        }
        return static_cast<float>(std::clamp(held / SPAN, FLOOR, 1.0));
    }

private:
    static constexpr double CAPACITOR = 4.7e-6; // C19, farads
    static constexpr double HIGH_PASS_HZ = 81.4; // twice: the amplifier's two input networks
    static constexpr double RECTIFIER_HZ = 1592.0; // 100 kΩ · 1 nF across the rectifier
    // The Threshold amplifier at 12:00, 3:00, 5:00: its gain (volts at the rectifier per
    // guitar's peak) and its bandwidth, Hz.
    static constexpr std::array<double, 3> LOG_GAIN{8.4987, 6.6297, 4.3704};
    static constexpr std::array<double, 3> LOG_BAND{6.8088, 8.5378, 9.9814};
    // The Decay pot with its series resistor at DECAY_MS's five positions, ohms (5.2 k, 21 k,
    // 38 k, 136 k, 178 k). 7:00, 12:00 and 5:00 from the session; 3:00 from the reamp of
    // 2026-11-08 (its two takes there, tools/capture/ns2/reamp_ab.py); 9:00 placed between
    // its neighbours as the first model's Decay was.
    static constexpr std::array<double, 5> LOG_OHMS{8.5622, 9.9776, 10.5471, 11.8200, 12.0888};
    static constexpr double CEILING = 2.6488; // the most the rectifier gives, volts
    // The diode into the capacitor: i = IS · (exp(V / ETA) − 1); the drop ahead of it (the
    // same diode at the 85 µA its load draws); the most its driver gives.
    static constexpr double CHARGE_ETA = 0.0985;
    static constexpr double CHARGE_IS = 3.0150e-07;
    static constexpr double CHARGE_DROP = 0.5558;
    static constexpr double CHARGE_MAX_A = 0.02;
    static constexpr double SPAN = 0.8355; // the capacitor's voltage at fully open
    static constexpr double FLOOR = 0.012; // closed: −38.4 dB
    // The pedal's own noise at the amplifier's input, re the guitar's peak (−106 dB): at a low
    // Threshold it keeps the gate a few dB off its floor.
    static constexpr double NOISE = 4.924e-06;

    double rate = 48000.0, step = 1.0 / 48000.0;
    double high_b = 1.0, high_a = 0.0, smooth = 1.0, charge_keep = 1.0, ramp = 0.0;
    double gain = 1.0, low_keep = 0.0, discharge = 0.0;
    double high1_in = 0.0, high1_out = 0.0, high2_in = 0.0, high2_out = 0.0;
    double low = 0.0, level = 0.0, held = 0.0;
};

inline float threshold_for(float opens_at_dbfs) {
    return std::clamp(opens_at_dbfs, OPENS_LOW_DBFS, OPENS_HIGH_DBFS);
}

inline float decay_for(float release_ms) {
    return std::clamp(release_ms, DECAY_MS.front(), DECAY_MS.back());
}

} // namespace ns2

} // namespace namx
