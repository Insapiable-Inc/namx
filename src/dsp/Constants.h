#pragma once
#include "Common.h"

namespace npl
{

// All knob parameter constants in one place.
// Edit here to change min/max/default values across the entire app.
namespace NAMConstants
{

// Input / Output gain
constexpr double GAIN_MIN = -20.0;
constexpr double GAIN_MAX = 20.0;
constexpr double GAIN_DEFAULT = 0.0;

// EQ bands
constexpr double EQ_BAND_MIN = -12.0;
constexpr double EQ_BAND_MAX = 12.0;
constexpr double EQ_BAND_DEFAULT = 0.0;

// Noise Gate
// Threshold reaches down to -100 dB so the knob's minimum is effectively
// "gate never closes"; -70 dB suits typical interface noise floors.
constexpr double GATE_THRESH_MIN = -100.0;
constexpr double GATE_THRESH_MAX = -20.0;
constexpr double GATE_THRESH_DEFAULT = -70.0;

// Fast attack so pick transients aren't clipped while the gate opens.
constexpr double GATE_ATTACK_MIN = 0.1;
constexpr double GATE_ATTACK_MAX = 50.0;
constexpr double GATE_ATTACK_DEFAULT = 1.0;

constexpr double GATE_RELEASE_MIN = 10.0;
constexpr double GATE_RELEASE_MAX = 1000.0;
constexpr double GATE_RELEASE_DEFAULT = 100.0;

// Compressor
constexpr double COMP_THRESH_MIN = -60.0;
constexpr double COMP_THRESH_MAX = 0.0;
constexpr double COMP_THRESH_DEFAULT = -20.0;

constexpr double COMP_RATIO_MIN = 1.0;
constexpr double COMP_RATIO_MAX = 20.0;
constexpr double COMP_RATIO_DEFAULT = 4.0;

constexpr double COMP_ATTACK_MIN = 0.1;
constexpr double COMP_ATTACK_MAX = 100.0;
constexpr double COMP_ATTACK_DEFAULT = 10.0;

constexpr double COMP_RELEASE_MIN = 10.0;
constexpr double COMP_RELEASE_MAX = 1000.0;
constexpr double COMP_RELEASE_DEFAULT = 100.0;

constexpr double COMP_MAKEUP_MIN = 0.0;
constexpr double COMP_MAKEUP_MAX = 24.0;
constexpr double COMP_MAKEUP_DEFAULT = 0.0;

// Chorus
constexpr double CHORUS_RATE_MIN = 0.1;
constexpr double CHORUS_RATE_MAX = 5.0;
constexpr double CHORUS_RATE_DEFAULT = 1.0;

constexpr double CHORUS_DEPTH_MIN = 0.0;
constexpr double CHORUS_DEPTH_MAX = 100.0;
constexpr double CHORUS_DEPTH_DEFAULT = 50.0;

constexpr double CHORUS_MIX_MIN = 0.0;
constexpr double CHORUS_MIX_MAX = 100.0;
constexpr double CHORUS_MIX_DEFAULT = 50.0;

// Flanger
// Flanging is a slow sweep; beyond ~2 Hz it turns into warbly vibrato.
constexpr double FLANGER_RATE_MIN = 0.05;
constexpr double FLANGER_RATE_MAX = 2.0;
constexpr double FLANGER_RATE_DEFAULT = 0.3;

constexpr double FLANGER_DEPTH_MIN = 0.0;
constexpr double FLANGER_DEPTH_MAX = 100.0;
constexpr double FLANGER_DEPTH_DEFAULT = 50.0;

constexpr double FLANGER_FEEDBACK_MIN = -99.0;
constexpr double FLANGER_FEEDBACK_MAX = 99.0;
constexpr double FLANGER_FEEDBACK_DEFAULT = 0.0;

constexpr double FLANGER_MIX_MIN = 0.0;
constexpr double FLANGER_MIX_MAX = 100.0;
constexpr double FLANGER_MIX_DEFAULT = 50.0;

// Phaser
constexpr double PHASER_RATE_MIN = 0.1;
constexpr double PHASER_RATE_MAX = 5.0;
constexpr double PHASER_RATE_DEFAULT = 1.0;

constexpr double PHASER_DEPTH_MIN = 0.0;
constexpr double PHASER_DEPTH_MAX = 100.0;
constexpr double PHASER_DEPTH_DEFAULT = 50.0;

constexpr double PHASER_FEEDBACK_MIN = 0.0;
constexpr double PHASER_FEEDBACK_MAX = 99.0;
constexpr double PHASER_FEEDBACK_DEFAULT = 50.0;

constexpr double PHASER_MIX_MIN = 0.0;
constexpr double PHASER_MIX_MAX = 100.0;
constexpr double PHASER_MIX_DEFAULT = 50.0;

// Tremolo
constexpr double TREMOLO_RATE_MIN = 0.5;
constexpr double TREMOLO_RATE_MAX = 15.0;
constexpr double TREMOLO_RATE_DEFAULT = 5.0;

constexpr double TREMOLO_DEPTH_MIN = 0.0;
constexpr double TREMOLO_DEPTH_MAX = 100.0;
constexpr double TREMOLO_DEPTH_DEFAULT = 50.0;

// Delay
// 20 ms floor keeps the delay line at least one sample long; 1 s covers
// musical delay times without drifting into looper territory.
constexpr double DELAY_TIME_MIN = 20.0;
constexpr double DELAY_TIME_MAX = 1000.0;
constexpr double DELAY_TIME_DEFAULT = 250.0;

constexpr double DELAY_FEEDBACK_MIN = 0.0;
constexpr double DELAY_FEEDBACK_MAX = 90.0;
constexpr double DELAY_FEEDBACK_DEFAULT = 40.0;

constexpr double DELAY_MIX_MIN = 0.0;
constexpr double DELAY_MIX_MAX = 100.0;
constexpr double DELAY_MIX_DEFAULT = 30.0;

// Reverb
constexpr double REVERB_ROOM_MIN = 0.0;
constexpr double REVERB_ROOM_MAX = 100.0;
constexpr double REVERB_ROOM_DEFAULT = 50.0;

constexpr double REVERB_DAMPING_MIN = 0.0;
constexpr double REVERB_DAMPING_MAX = 100.0;
constexpr double REVERB_DAMPING_DEFAULT = 50.0;

constexpr double REVERB_WIDTH_MIN = 0.0;
constexpr double REVERB_WIDTH_MAX = 100.0;
constexpr double REVERB_WIDTH_DEFAULT = 100.0;

constexpr double REVERB_MIX_MIN = 0.0;
constexpr double REVERB_MIX_MAX = 100.0;
constexpr double REVERB_MIX_DEFAULT = 30.0;

// Dynamic Wah
constexpr double WAH_SENS_MIN = 0.0;
constexpr double WAH_SENS_MAX = 100.0;
constexpr double WAH_SENS_DEFAULT = 70.0;

constexpr double WAH_RES_MIN = 0.0;
constexpr double WAH_RES_MAX = 100.0;
constexpr double WAH_RES_DEFAULT = 60.0;

constexpr double WAH_MIX_MIN = 0.0;
constexpr double WAH_MIX_MAX = 100.0;
constexpr double WAH_MIX_DEFAULT = 100.0;

// Drive (Phase 17)
constexpr double DRIVE_MIN = 0.0;
constexpr double DRIVE_MAX = 100.0;
constexpr double DRIVE_DEFAULT = 30.0;
constexpr double DRIVE_TONE_MIN = 0.0;
constexpr double DRIVE_TONE_MAX = 100.0;
constexpr double DRIVE_TONE_DEFAULT = 50.0;
constexpr double DRIVE_LEVEL_MIN = -20.0;
constexpr double DRIVE_LEVEL_MAX = 20.0;
constexpr double DRIVE_LEVEL_DEFAULT = 0.0;

// Doubler
constexpr double DOUBLER_SPREAD_MIN = 3.0;
constexpr double DOUBLER_SPREAD_MAX = 20.0;
constexpr double DOUBLER_SPREAD_DEFAULT = 10.0;
constexpr double DOUBLER_MIX_MIN = 0.0;
constexpr double DOUBLER_MIX_MAX = 100.0;
constexpr double DOUBLER_MIX_DEFAULT = 100.0;

// Volume pedal
constexpr double VOLUME_POS_MIN = 0.0;
constexpr double VOLUME_POS_MAX = 100.0;
constexpr double VOLUME_POS_DEFAULT = 100.0;
constexpr double VOLUME_MID_MIN = -40.0;
constexpr double VOLUME_MID_MAX = 0.0;
constexpr double VOLUME_MID_DEFAULT = -12.0;

// Phase 17: EQ frequencies + cuts
constexpr double EQ_BASS_FREQ_MIN = 40.0, EQ_BASS_FREQ_MAX = 500.0, EQ_BASS_FREQ_DEFAULT = 250.0;
constexpr double EQ_MID_FREQ_MIN = 200.0, EQ_MID_FREQ_MAX = 2500.0, EQ_MID_FREQ_DEFAULT = 1000.0;
constexpr double EQ_HI_MID_FREQ_MIN = 800.0, EQ_HI_MID_FREQ_MAX = 8000.0,
                 EQ_HI_MID_FREQ_DEFAULT = 3000.0;
constexpr double EQ_TREBLE_FREQ_MIN = 1500.0, EQ_TREBLE_FREQ_MAX = 12000.0,
                 EQ_TREBLE_FREQ_DEFAULT = 4000.0;
constexpr double LOW_CUT_MIN = 20.0, LOW_CUT_MAX = 500.0, LOW_CUT_DEFAULT = 20.0; // 20 = off
constexpr double HIGH_CUT_MIN = 2000.0, HIGH_CUT_MAX = 20000.0,
                 HIGH_CUT_DEFAULT = 20000.0; // 20k = off

// Phase 17: Delay
constexpr double DELAY_RIGHT_MIN = 25.0, DELAY_RIGHT_MAX = 200.0,
                 DELAY_RIGHT_DEFAULT = 100.0; // % of Time
constexpr double DELAY_CROSS_MIN = 0.0, DELAY_CROSS_MAX = 100.0,
                 DELAY_CROSS_DEFAULT = 100.0; // 100 = ping-pong
constexpr double DELAY_LOW_CUT_MAX = 1000.0;
constexpr double DELAY_HIGH_CUT_MIN = 1000.0;
constexpr double DELAY_SAT_MIN = 0.0, DELAY_SAT_MAX = 100.0, DELAY_SAT_DEFAULT = 0.0;
constexpr double DELAY_MOD_MIN = 0.0, DELAY_MOD_MAX = 100.0, DELAY_MOD_DEFAULT = 0.0;

// Phase 17: Reverb
constexpr double REVERB_PRE_DELAY_MIN = 0.0, REVERB_PRE_DELAY_MAX = 250.0,
                 REVERB_PRE_DELAY_DEFAULT = 0.0;
constexpr double REVERB_LOW_CUT_MAX = 1000.0;
constexpr double REVERB_HIGH_CUT_MIN = 1000.0;

// Phase 17: modulation modes / ranges
constexpr double FLANGER_RANGE_MIN = 0.0, FLANGER_RANGE_MAX = 100.0, FLANGER_RANGE_DEFAULT = 100.0;
constexpr double WAH_POSITION_MIN = 0.0, WAH_POSITION_MAX = 100.0, WAH_POSITION_DEFAULT = 50.0;

} // namespace NAMConstants

} // namespace npl
