#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "BiquadChain.h"
#include "Common.h"

namespace namx
{

// The Boss NS-2 Noise Suppressor (Mode: Reduction), fitted to captures of a real one
// (2026-10-27; the study and its data are in captures/boss/ns-2/README.md,
// tools/capture/analyze_ns2.py measures and fits them). Not a hard gate: a switch that opens
// at once and a slow expander under it.
//
//   L      the input's level (a peak envelope; dB re REFERENCE_DBFS)
//   E      fast level: rises with L at once, falls toward it exponentially in dB (Decay)
//   V      slow level: the same, falling RATIO times slower
//   switch opens once E > T + OPEN_DB, closes once E < T + OPEN_DB − HYSTERESIS_DB
//   G      0 dB while open; closed, it sinks toward −JUMP_DB (Decay × CLOSE)
//   gain   = OPEN_GAIN_DB + G − SLOPE · max(0, T − V)
//
// So a note opens it fully at once; as the note dies the switch holds while E falls (Decay),
// then the gain slides down — first JUMP_DB, then deeper as V sinks under the threshold.
namespace ns2
{

// A hard-played guitar's peak, as for the other pedal models (Dd8Delay.h, Cs3Compressor.h):
// an assumption — the pedal sat before the amp.
constexpr float REFERENCE_DBFS = -6.0f;

constexpr float JUMP_DB = 7.56f;
constexpr float OPEN_DB = 1.29f;
constexpr float HYSTERESIS_DB = 10.47f;
constexpr float CLOSE = 4.42f;
constexpr float SLOPE = 0.889f;
constexpr float RATIO = 7.94f;
constexpr float OPEN_GAIN_DB = 0.6f;

// The open path's top end, re its 1 kHz gain: flat to 3 kHz, −1.3 dB at 8 kHz, −2.4 at
// 12 kHz, −3.1 at 20 kHz (within 0.09 dB; tools/capture/fit_voicings.py, 2026-10-29). A low
// shelf that BiquadChain scales to unity at 1 kHz: in effect a −3.7 dB high shelf.
constexpr size_t TONE_SECTIONS = 1;
constexpr FilterChain<TONE_SECTIONS> TONE{{
    {FilterKind::LOW_SHELF, 8168.4f, 0.623f, 3.69f},
}};

// Threshold and Decay at 7:00, 9:00, 12:00, 3:00, 5:00: T (dB re REFERENCE_DBFS) and the
// fast level's fall (ms). Threshold does little below 12:00.
constexpr std::array<float, 5> THRESHOLD_DB{-69.40f, -69.02f, -66.72f, -49.40f, -27.33f};
constexpr std::array<float, 5> DECAY_MS{24.6f, 63.5f, 93.0f, 145.1f, 460.3f};

// The Threshold knob is where the gate opens, in dBFS: the pedal's span.
constexpr float OPENS_LOW_DBFS = THRESHOLD_DB.front() + OPEN_DB + REFERENCE_DBFS;
constexpr float OPENS_HIGH_DBFS = THRESHOLD_DB.back() + OPEN_DB + REFERENCE_DBFS;

// The input's level: a peak follower whose 50 ms fall keeps a low E's ripple to ~2 dB.
constexpr float PEAK_RELEASE_MS = 50.0f;
constexpr float FLOOR_DB = -140.0f;

inline float threshold_for(float opens_at_dbfs) {
    return std::clamp(opens_at_dbfs, OPENS_LOW_DBFS, OPENS_HIGH_DBFS) - REFERENCE_DBFS - OPEN_DB;
}

inline float decay_for(float release_ms) {
    return std::clamp(release_ms, DECAY_MS.front(), DECAY_MS.back());
}

} // namespace ns2

} // namespace namx
