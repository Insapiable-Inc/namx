#pragma once
#include "Common.h"

namespace namx
{

// All knob parameter constants in one place.
// Edit here to change min/max/default values across the entire app.
namespace NAMConstants
{

// Input / Output gain
constexpr double GAIN_MIN = -20.0;
constexpr double GAIN_MAX = 20.0;
constexpr double GAIN_DEFAULT = 0.0;

// Noise Gate: the NS-2's spans (Ns2Gate.h) — Threshold 7:00 … 5:00 as where it opens
// (ns2::OPENS_LOW_DBFS / _HIGH_DBFS), Decay 7:00 … 5:00 (ns2::DECAY_MS). -70 dB suits typical
// interface noise floors.
constexpr double GATE_THRESH_MIN = -74.11;
constexpr double GATE_THRESH_MAX = -32.04;
constexpr double GATE_THRESH_DEFAULT = -70.0;

constexpr double GATE_RELEASE_MIN = 24.6;
constexpr double GATE_RELEASE_MAX = 460.3;
constexpr double GATE_RELEASE_DEFAULT = 100.0;

// Compressor
// The CS-3's spans (Cs3Compressor.h): Sustain 5:00 … 7:00 as the input level that reaches the
// ceiling (REFERENCE_DBFS + THRESHOLD_LOW_DB / _HIGH_DB), Attack 5:00 … 7:00 as the time scale
// of the gain's recovery (RELEASE_MS; the default is Attack 12:00). The default Threshold is Sustain 7:00, as the old −20 dB clamped to.
constexpr double COMP_THRESH_MIN = -55.73;
constexpr double COMP_THRESH_MAX = -20.26;
constexpr double COMP_THRESH_DEFAULT = -20.26;

constexpr double COMP_RELEASE_MIN = 99.5;
constexpr double COMP_RELEASE_MAX = 1152.0;
constexpr double COMP_RELEASE_DEFAULT = 300.0;

constexpr double COMP_MAKEUP_MIN = 0.0;
constexpr double COMP_MAKEUP_MAX = 24.0;
constexpr double COMP_MAKEUP_DEFAULT = 0.0;

// Chorus
constexpr double CHORUS_RATE_MIN = 0.25; // the CE-5's Rate at 7:00 and 5:00 (ce5::RATE_HZ)
constexpr double CHORUS_RATE_MAX = 5.934;
constexpr double CHORUS_RATE_DEFAULT = 1.0;

constexpr double CHORUS_DEPTH_MIN = 0.0;
constexpr double CHORUS_DEPTH_MAX = 100.0;
constexpr double CHORUS_DEPTH_DEFAULT = 58.0; // the CE-5's Depth at 12:00

// Level: the chorus re the dry, dB. The CE-5's E.Level spans -21.2 (it never mutes) to +2.35;
// the bottom of ours is Off.
constexpr double CHORUS_LEVEL_MIN = -24.0;
constexpr double CHORUS_LEVEL_MAX = 2.4;
constexpr double CHORUS_LEVEL_DEFAULT = -10.8; // the CE-5's E.Level at 12:00, the knob's centre
// The CE-5's Low and High filters (on the chorus only), as cut frequencies: where the chorus is
// 3 dB down (ce5::LOW_CUT_HZ, HIGH_HZ). The ends away from the cut are Off.
constexpr double CHORUS_LOW_CUT_MIN = 50.0, CHORUS_LOW_CUT_MAX = 771.0,
                 CHORUS_LOW_CUT_DEFAULT = 50.0;
constexpr double CHORUS_HIGH_CUT_MIN = 640.0, CHORUS_HIGH_CUT_MAX = 20000.0,
                 CHORUS_HIGH_CUT_DEFAULT = 20000.0;

// Flanger
// Flanging is a slow sweep; beyond ~2 Hz it turns into warbly vibrato.
constexpr double FLANGER_RATE_MIN = 0.0553; // the BF-3's Rate at 7:00 and 5:00 (bf3::RATE_HZ)
constexpr double FLANGER_RATE_MAX = 10.0;
constexpr double FLANGER_RATE_DEFAULT = 0.3;

constexpr double FLANGER_DEPTH_MIN = 0.0;
constexpr double FLANGER_DEPTH_MAX = 100.0;
constexpr double FLANGER_DEPTH_DEFAULT = 34.0; // the BF-3's Depth at 12:00

constexpr double FLANGER_FEEDBACK_MIN =
    0.0; // the BF-3's Res, as the gain per pass round its loop, %
constexpr double FLANGER_FEEDBACK_MAX = 89.0;
constexpr double FLANGER_FEEDBACK_DEFAULT = 0.0;

// Phaser
// The PH-3's range: 0 = stopped (the sweep freezes where it is: the pedal's knob fully
// down), then 0.047 Hz to 21.5 Hz (captures/boss/ph-3/README.md → Rate).
constexpr double PHASER_RATE_MIN = 0.0;
constexpr double PHASER_RATE_SLOWEST = 0.047; // the pedal's slowest sweep
constexpr double PHASER_RATE_MAX = 21.5;
constexpr double PHASER_RATE_DEFAULT = 0.4;

constexpr double PHASER_DEPTH_MIN = 0.0;
constexpr double PHASER_DEPTH_MAX = 100.0;
constexpr double PHASER_DEPTH_DEFAULT = 48.0; // the PH-3's Depth at 12:00

constexpr double PHASER_FEEDBACK_MIN = 0.0;
constexpr double PHASER_FEEDBACK_MAX = 75.0; // the loop's gain per pass (ph3::LOOP_GAIN_TOP)
constexpr double PHASER_FEEDBACK_DEFAULT = 0.0;

// Tremolo
constexpr double TREMOLO_RATE_MIN = 0.5;
constexpr double TREMOLO_RATE_MAX = 15.0;
constexpr double TREMOLO_RATE_DEFAULT = 5.0;

constexpr double TREMOLO_DEPTH_MIN = 0.0;
constexpr double TREMOLO_DEPTH_MAX = 100.0;
constexpr double TREMOLO_DEPTH_DEFAULT = 44.0; // the TR-2's Depth at 12:00

// Wave: 0–100 %, triangle → as square as the TR-2 gets at the rate (a log curve).
constexpr double TREMOLO_WAVE_MIN = 0.0;
constexpr double TREMOLO_WAVE_MAX = 100.0;
constexpr double TREMOLO_WAVE_DEFAULT = 45.0; // the TR-2's Wave at 12:00 (at 5 Hz)

// Delay
// 20 ms floor keeps the delay line at least one sample long; 3 s covers synced
// note values at slow tempos (a dotted half at 80 BPM is 1125 ms) without drifting
// into looper territory (Gojira X: 3 s; Petrucci X: 6 s).
constexpr double DELAY_TIME_MIN = 20.0;
constexpr double DELAY_TIME_MAX = 3000.0;
constexpr double DELAY_TIME_DEFAULT = 250.0;
constexpr double DELAY_RIGHT_TIME_DEFAULT = 500.0; // twice Time: the DD-8's even ping-pong

constexpr double DELAY_FEEDBACK_MIN = 0.0;
constexpr double DELAY_FEEDBACK_MAX = 100.0; // the gain per round: 100 % holds
constexpr double DELAY_FEEDBACK_DEFAULT = 40.0;

// The first repeat re the dry, dB (the DD-8's E.Level reaches −29 to +4.5 … +7.9 by mode); the
// default sits at the knob's centre;
// the lowest setting is off.
constexpr double DELAY_LEVEL_MIN = -30.0;
constexpr double DELAY_LEVEL_MAX = 6.0;
constexpr double DELAY_LEVEL_DEFAULT = -12.0;

// Reverb
constexpr double REVERB_ROOM_MIN = 0.0;
constexpr double REVERB_ROOM_MAX = 100.0;
constexpr double REVERB_ROOM_DEFAULT = 50.0;

constexpr double REVERB_DAMPING_MIN = 0.0;
constexpr double REVERB_DAMPING_MAX = 100.0;
constexpr double REVERB_DAMPING_DEFAULT = 50.0;

constexpr double REVERB_MIX_MIN = 0.0;
constexpr double REVERB_MIX_MAX = 100.0;
constexpr double REVERB_MIX_DEFAULT = 30.0;

// Dynamic Wah
constexpr double WAH_SENS_MIN = 0.0;
constexpr double WAH_SENS_MAX = 100.0;
constexpr double WAH_SENS_DEFAULT = 70.0;

constexpr double WAH_MIX_MIN = 0.0;
constexpr double WAH_MIX_MAX = 100.0;
constexpr double WAH_MIX_DEFAULT = 100.0;

// Amplifier Input (how hard the model is hit) and Output (a trim on top of the loudness
// normalization)
constexpr double NAM_INPUT_MIN = -20.0;
constexpr double NAM_INPUT_MAX = 20.0;
constexpr double NAM_INPUT_DEFAULT = 0.0;
constexpr double NAM_OUTPUT_MIN = -20.0;
constexpr double NAM_OUTPUT_MAX = 20.0;
constexpr double NAM_OUTPUT_DEFAULT = 0.0;

// Cabinet Level (slot A; preset key ir/levelA)
constexpr double IR_LEVEL_MIN = -20.0;
constexpr double IR_LEVEL_MAX = 20.0;
constexpr double IR_LEVEL_DEFAULT = 0.0;

// Drive (Phase 17)
constexpr double DRIVE_MIN = 0.0;
constexpr double DRIVE_MAX = 100.0;
constexpr double DRIVE_DEFAULT = 25.0;
constexpr double DRIVE_TONE_MIN = 0.0;
constexpr double DRIVE_TONE_MAX = 100.0;
constexpr double DRIVE_TONE_DEFAULT = 55.0;
constexpr double DRIVE_LEVEL_MIN = -20.0;
constexpr double DRIVE_LEVEL_MAX = 20.0;
constexpr double DRIVE_LEVEL_DEFAULT = 6.0;

// Volume pedal
constexpr double VOLUME_POS_MIN = 0.0;
constexpr double VOLUME_POS_MAX = 100.0;
constexpr double VOLUME_POS_DEFAULT = 100.0;
constexpr double VOLUME_MID_MIN = -40.0;
constexpr double VOLUME_MID_MAX = 0.0;
constexpr double VOLUME_MID_DEFAULT = -12.0;

// Phase 17: the cuts (EQ)
constexpr double LOW_CUT_MIN = 20.0, LOW_CUT_MAX = 500.0, LOW_CUT_DEFAULT = 20.0; // 20 = off
constexpr double HIGH_CUT_MIN = 2000.0, HIGH_CUT_MAX = 20000.0,
                 HIGH_CUT_DEFAULT = 20000.0; // 20k = off

// Phase 17: Reverb

// Phase 17: modulation modes / ranges
// The BF-3's Manual: the flanged copy's delay with Depth at 0, ms (bf3::MANUAL_MS's ends).
constexpr double FLANGER_MANUAL_MIN = 0.447, FLANGER_MANUAL_MAX = 14.673,
                 FLANGER_MANUAL_DEFAULT = 14.673;
constexpr double WAH_POSITION_MIN = 0.0, WAH_POSITION_MAX = 100.0, WAH_POSITION_DEFAULT = 50.0;

} // namespace NAMConstants

} // namespace namx
