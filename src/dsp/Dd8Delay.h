#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "BiquadChain.h"
#include "Common.h"

namespace namx
{

// The Boss DD-8 Digital Delay's Standard, Analog, Tape, Warm and Mod modes, as voicings
// of one engine, from captures of a real one (2026-10-26/27; the study and its data are
// in captures/boss/dd-8/README.md — "The four voicings side by side" is this file's
// table). Per line:
//
//   r    = loop chain( line read at the delay time (± the voicing's LFO) )
//   wet  = Level · post chain( clip( repeat gain · out chain(r) ) )
//   line ← limit( input + Feedback · feedback chain( loop low-pass(r) ), FEEDBACK_DELAY_MS late )
//   out  = dry + wet                               the dry path is untouched (analog)
//
// The loop chain acts on every pass, the first repeat included; the out chain and the
// mode's repeat gain shape what is heard, ahead of the clip; the feedback chain and the
// loop low-pass (the small extra high cut per pass measured in Standard) act from the
// second repeat on. Analog's and Tape's chains were refitted 2026-11-10 from the first
// repeat and the passes after it (tools/capture/dd8/refit.py) and checked on guitar.
//
// One clip for every mode (the loop session, 2026-11-11: ladders of bursts at 1 kHz and
// 300 Hz, each with its repeats, captures/boss/dd-8/README.md): a hard ceiling
// OUT_CEILING_DB over the reference on each repeat as it leaves the line, after the mode's
// own tone and gain and before Level. So a mode with hotter repeats clips sooner (Tape
// 3.4 dB before Standard), and Warm's bass lift clips low notes sooner than high ones —
// with nothing fitted per mode the first repeat's loss, the second's level and their
// third harmonics follow the pedal's within 0.2 dB rms in all five. The line holds the
// signal whole up to its own hard limit, line_db (2 dB over the reference: what holds the
// loop at Feedback 5:00); Analog's is a soft saturation lower down. The levels are re
// REFERENCE_DBFS, which stands for the hard guitar peak the pedal was measured against.
// Mono and allocation-free; the caller keeps the filter state.
namespace dd8
{

// A hard-played guitar's peak after a loudness-normalized amp model (-18 dB): where
// the pedal's clip is calibrated from. An assumption — the pedal sat before the amp.
constexpr float REFERENCE_DBFS = -6.0f;

// The clip on every repeat: a hard ceiling, re REFERENCE_DBFS, on the repeat at its mode's
// gain (repeat_db), before the Level knob.
constexpr float OUT_CEILING_DB = 1.2f;

// Every pass round the loop takes this much longer than the first repeat's delay: the
// pedal's repeats come at T, 2T + 16.5 samples, 3T + 33… (48 kHz; the same at Time 9:00,
// 12:00 and 3:00, in Standard, Warm and Tape — its feedback path's own latency; 16.44 and
// 16.57 samples on the loop session's bursts at 1 kHz and 300 Hz). Without it the model's
// later repeats run ahead and overlap each other and the dry differently. Here 16 samples
// at 48 kHz; the model's loop low-pass adds a quarter of one.
constexpr float FEEDBACK_DELAY_MS = 0.339f;

// Tape's wow (a held tone's repeat, 28 s, October's hold-sine read again 2026-11-11): not
// a sine. Every WANDER_TICK_S the pedal takes a new pitch offset and holds it, the change
// over ~15 ms — steps between about −3.2 and +2.4 cents, one draw independent of the last
// (1.68 cents rms, as measured before; the edges sit on that grid within 2 % of a tick).
// The offsets here: 16 equally likely values, the measured ones' quantiles. Each is a slope
// of the delay, so the delay itself wanders; the pedal's stayed within ±0.75 ms, which a
// free walk wouldn't: WANDER_RETURN of the way back per tick keeps it there, and
// WANDER_MAX_MS is the stop.
constexpr float WANDER_TICK_S = 0.18586f;
constexpr std::array<float, 16> WANDER_CENTS{-3.22f, -2.82f, -2.37f, -1.37f, -0.48f, 0.0f,
                                             0.0f,   0.0f,   0.38f,  0.48f,  0.89f,  0.95f,
                                             1.28f,  1.78f,  2.09f,  2.35f};
constexpr float WANDER_EDGE_MS = 6.0f; // the one-pole on the pitch (10–90 % in ~13 ms)
constexpr float WANDER_RETURN = 0.2f;
constexpr float WANDER_MAX_MS = 1.5f;

// The positions the knob maps are measured at: 9:00, 12:00, 3:00, 5:00 (fraction of the
// rotation). At 7:00 both Level and Feedback are off.
constexpr std::array<float, 4> KNOB_AT{0.2f, 0.5f, 0.8f, 1.0f};

// Level, dB re 5:00 (an audio taper; the same in every mode within ~1 dB).
// 12:00 was −13.4 in October; three later sessions' hand-set 12:00 all read 1–2 dB under it.
constexpr std::array<float, 4> LEVEL_DB{-33.6f, -14.8f, -2.6f, 0.0f};

constexpr size_t CHAIN = 4;
using Chain = FilterChain<CHAIN>;

struct Voicing
{
    float repeat_db = 0.0f; // the first repeat over the dry at Level 5:00
    float line_db = 2.0f; // the line's limit on the way in, re REFERENCE_DBFS
    float line_knee = 0.0f; // 0: hard; else the soft clip's exponent n (x / (1 + |x/c|^n)^(1/n))
    float advance_ms =
        0.0f; // the line read this much early: the pedal makes up for its filters' delay
    std::array<float, 4> feedback_db{}; // per repeat at 1 kHz, at KNOB_AT
    Chain loop{}; // on every pass
    Chain out{}; // on the output only, ahead of the clip
    Chain post{}; // on the output only, after the clip
    Chain feedback{}; // on the way back into the line only
    float loop_low_pass_hz = 0.0f; // first-order, on the feedback path (0: none)
    float lfo_hz = 0.0f; // sine on the delay time (Mod; Tape's wow)
    float lfo_ms = 0.0f; // its depth, ± ms
    float flutter_hz = 0.0f; // a second sine (Tape)
    float flutter_ms = 0.0f;
    bool wander = false; // Tape's wow: the pitch steps of WANDER_*, below
};

// The first repeat's tone: 1.3 dB under its 1 kHz level below 250 Hz, a hump of +1.1 dB at
// 1.6 kHz, −4.6 dB at 2.5 kHz, −19 dB at 4 kHz, −38 dB at 6.3 kHz (within 0.14 dB from 60 Hz
// to 5 kHz), and the same again on every pass. With the hump in the loop the mids outlast the
// rest at high Feedback, as on the pedal; the Feedback map below is at 1 kHz, 1.1 dB under it.
// The clip sits inside that filter: the shelf and the resonant section ahead of it, the
// other low-pass after it (ANALOG_AFTER: on the output, and on the way back) — the pedal's
// first repeat of a clipped chirp keeps harmonics at 4–9 kHz that the whole filter after the
// clip would remove (21 dB short at 6–9 kHz) and the whole filter before it would leave
// 9 dB too strong; this split is within 5 dB.
constexpr Chain ANALOG_LOOP{{
    {FilterKind::LOW_SHELF, 2773.0f, 0.5f, 2.01f},
    {FilterKind::LOW_PASS, 2005.8f, 1.2808f, 0.0f},
    {},
    {},
}};
constexpr FilterSection ANALOG_AFTER{FilterKind::LOW_PASS, 2987.6f, 0.7675f, 0.0f};

// The first repeat's tone, re 1 kHz: +0.3 dB below 150 Hz, flat to 3 kHz, −0.6 dB at 5 kHz,
// −2.1 dB at 8 kHz, −5.5 dB at 10 kHz (within 0.05 dB, 45 Hz – 10 kHz). Refitted 2026-11-11
// through a 170 ms window (October's 21 ms one read the lows 1 dB high: a +1.9 dB peak at
// 60 Hz that the guitar takes showed as 0.6 dB too much at 100 Hz).
constexpr Chain STANDARD_OUT{{
    {FilterKind::LOW_PASS, 9038.2f, 0.5663f, 0.0f},
    {FilterKind::LOW_PASS, 10163.7f, 1.0718f, 0.0f},
    {FilterKind::LOW_SHELF, 651.5f, 0.5f, 0.37f},
    {},
}};

// Feedback at KNOB_AT, dB per repeat at 1 kHz: one knob law for Standard, Warm and Mod (the
// loop session: on one knob setting the three fade alike, −7.4 dB at 12:00 and −0.82 at 3:00;
// October's separate values were three hands). Analog is 0.4 dB under it at 1 kHz (its hump
// 1.1 over that), Tape 0.85 over: at 5:00 Tape's loop is over unity and rings up at its
// 1.2 kHz peak, as the pedal's does. At 5:00 the three are 0.07 dB over unity: a burst's
// repeats grow into the line's limit and come out squarer and squarer over ~30 passes.
constexpr std::array<float, 4> FEEDBACK_DB{-12.2f, -7.45f, -0.82f, 0.07f};

constexpr float LOOP_LOW_PASS_HZ = 13000.0f; // −1.4 dB at 8 kHz per pass

constexpr Voicing STANDARD{
    .repeat_db = 4.46f,
    .feedback_db = FEEDBACK_DB,
    .out = STANDARD_OUT,
    .loop_low_pass_hz = LOOP_LOW_PASS_HZ,
};

// Darker on every pass, repeats 1.6 dB hotter, a soft knee; every pass also loses bass (a
// first-order high-pass at 103 Hz on the way back: −3 dB at 100 Hz, −1 dB at 200 Hz per
// repeat). Feedback 5:00 runs away slowly (+0.4 dB per repeat at 1 kHz), held by the clip.
// Feedback 3:00 is −1.1 dB, not October's −1.40 (two sessions, two hands): in the focused
// session the knob stayed put between Standard's take and Analog's, and against Standard's
// −0.67 dB there Analog's mids ring on as −1.1 gives (2026-11-10).
constexpr Voicing ANALOG{
    .repeat_db = 6.03f,
    .line_db = -2.0f,
    .line_knee = 4.3f,
    .advance_ms = 0.2444f,
    .feedback_db = {-12.6f, -7.87f, -1.17f, -0.33f},
    .loop = ANALOG_LOOP,
    .post = {{ANALOG_AFTER, {}, {}, {}}},
    .feedback = {{ANALOG_AFTER,
                  {FilterKind::HIGH_PASS_1, 102.7f, 0.707f, 0.0f},
                  {FilterKind::LOW_SHELF, 1100.0f, 0.7f,
                   -0.08f}, // the tail at Feedback 3:00: 0.08 dB a pass
                  {}}},
};

// A band: the first repeat −14.2 / −8.4 / −1.9 dB at 63 / 100 / 200 Hz, flat to 2 kHz,
// −4.7 dB at 4 kHz, −14.1 dB at 8 kHz (within 0.4 dB; the low cut on the output, the high
// cut in the loop), and every pass narrower: −2.6 dB at 100 Hz, −1.7 dB at 400 Hz, −2.9 dB
// at 2.5 kHz, −6.7 dB at 4 kHz, −14.7 dB at 6.3 kHz re 1 kHz (within 0.2 dB; on guitar the
// repeats' top is ~1 dB per pass brighter than this). Wow: the pitch steps of
// WANDER_* (until 2026-11-11 a 0.53 Hz sine of the same rms plus a 6.7 Hz "flutter": that
// line in the pitch's spectrum is the staircase's own — the pedal's steps are flat). The measured 2.4 dB level wobble is left out (its phase against
// the wow is unknown).
constexpr Voicing TAPE{
    .repeat_db = 7.9f,
    .feedback_db = {-11.35f, -6.6f, -0.03f, 0.92f},
    .loop = {{{FilterKind::LOW_PASS, 3957.9f, 0.5941f, 0.0f}, {}, {}, {}}},
    .out = {{
        {FilterKind::HIGH_PASS, 136.1f, 0.5476f, 0.0f},
        {FilterKind::LOW_PASS, 12046.9f, 0.7520f, 0.0f},
        {},
        {},
    }},
    .feedback = {{
        {FilterKind::PEAK, 1228.7f, 0.7816f, 2.36f},
        {FilterKind::HIGH_PASS_1, 46.4f, 0.707f, 0.0f},
        {FilterKind::LOW_PASS_1, 6900.3f, 0.707f, 0.0f},
        {},
    }},
    .wander = true,
};

// Standard with a warm tilt ahead of the clip (so low notes clip sooner than high ones, as
// the pedal's ladders show): +1.8 dB below 200 Hz, −2.1 dB at 4 kHz, −4.8 dB at 8 kHz
// (within 0.05 dB; its sweep of 2026-11-11 through a 170 ms window — October read the lows
// 1 dB high), 1.6 dB quieter. Its loop is Standard's: the tilt isn't in it.
constexpr Voicing WARM{
    .repeat_db = 2.85f,
    .feedback_db = FEEDBACK_DB,
    .out = {{
        {FilterKind::LOW_SHELF, 913.7f, 0.4933f, 3.29f},
        {FilterKind::LOW_PASS, 9895.1f, 0.5390f, 0.0f},
        {FilterKind::LOW_PASS, 23520.0f, 0.4006f, 0.0f},
        {},
    }},
    .loop_low_pass_hz = LOOP_LOW_PASS_HZ,
};

// Standard plus a 1.51 Hz sine of ±0.62 ms on the delay (the same at every Time). Its
// tone couldn't be measured (the LFO smears the sweeps): Standard's, as everything else
// matches Standard.
constexpr Voicing MOD{
    .repeat_db = 4.47f,
    .feedback_db = FEEDBACK_DB,
    .out = STANDARD_OUT,
    .loop_low_pass_hz = LOOP_LOW_PASS_HZ,
    .lfo_hz = 1.514f,
    .lfo_ms = 0.63f,
};

inline float db_to_gain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

// A knob at `fraction` of its rotation through a map in dB at KNOB_AT, linear in dB in
// between; below 9:00 the gain falls linearly to 0 at 7:00.
inline float knob_gain(const std::array<float, 4>& db, float fraction) {
    const float x = std::clamp(fraction, 0.0f, 1.0f);
    if (x < KNOB_AT[0]) {
        return x / KNOB_AT[0] * db_to_gain(db[0]);
    }
    size_t i = 0;
    while (i + 2 < KNOB_AT.size() && x > KNOB_AT[i + 1]) {
        ++i;
    }
    const float t = (x - KNOB_AT[i]) / (KNOB_AT[i + 1] - KNOB_AT[i]);
    return db_to_gain(db[i] + t * (db[i + 1] - db[i]));
}

// The clip, with the ceiling as a linear amplitude.
inline float clip(float x, float ceiling, float knee) {
    if (knee <= 0.0f) {
        return std::clamp(x, -ceiling, ceiling);
    }
    const float u = std::abs(x) / ceiling;
    if (knee > 24.0f && u < 0.5f) {
        return x; // (1/2)^n is below float precision for a knee that sharp
    }
    return x / std::pow(1.0f + std::pow(u, knee), 1.0f / knee);
}

// A voicing's filters at one sample rate.
struct Coefficients
{
    BiquadChain<CHAIN> loop;
    BiquadChain<CHAIN> out;
    BiquadChain<CHAIN> feedback;
    BiquadChain<CHAIN> post;
    float loop_low_pass = 1.0f; // one-pole coefficient (1: pass-through)
    float loop_peak = 1.0f; // the loop's gain at its loudest frequency, re 1 kHz

    void prepare(const Voicing& v, double sr) {
        loop.design(v.loop, sr);
        out.design(v.out, sr);
        feedback.design(v.feedback, sr);
        post.design(v.post, sr);
        loop_low_pass = v.loop_low_pass_hz > 0.0f
                            ? static_cast<float>(
                                  1.0 - std::exp(-2.0 * std::numbers::pi * v.loop_low_pass_hz / sr))
                            : 1.0f;
        // Where the loop is loudest (Analog's hump, Tape's mid peak; the lows elsewhere):
        // Feedback is the gain per round there, so 100 % holds and nothing grows.
        double peak = 0.0;
        for (double hz = 30.0; hz < 0.45 * sr; hz *= 1.02) {
            const double w = 2.0 * std::numbers::pi * hz / sr;
            const double pole = 1.0 - loop_low_pass;
            const double one_pole =
                loop_low_pass / std::sqrt(1.0 - 2.0 * pole * std::cos(w) + pole * pole);
            peak = std::max(peak, loop.magnitude(hz, sr) * feedback.magnitude(hz, sr) * one_pole);
        }
        loop_peak = static_cast<float>(peak);
    }
};

// The DD-8's knobs as NAM-X's (for captures, and for presets saved when NAM-X's knobs were
// the pedal's positions, 0–1 = 7:00–5:00). NAM-X's are plain curves over the pedal's range:
// Level is the first repeat re the dry at 1 kHz, in dB (the pedal's pot runs 20 dB between
// 9:00 and 12:00); Feedback is the gain per round at the loop's loudest frequency, in %,
// the same in every mode — 100 % holds; the pedal's last stretch over unity (a burst
// growing into the line's limit, Tape and Analog ringing up) is left out.
inline float level_db_from_pedal(const Voicing& v, float fraction) {
    return 20.0f * std::log10(std::max(knob_gain(LEVEL_DB, fraction), 1e-6f)) + v.repeat_db;
}

inline float feedback_pct_from_pedal(const Voicing& v, float fraction, double sr = 48000.0) {
    Coefficients k;
    k.prepare(v, sr);
    return std::min(100.0f, 100.0f * knob_gain(v.feedback_db, fraction) * k.loop_peak);
}

// The voicings in Type order (the Delay's Type 1–5).
inline constexpr std::array<const Voicing*, 5> VOICINGS{&STANDARD, &ANALOG, &TAPE, &WARM, &MOD};

} // namespace dd8

} // namespace namx
