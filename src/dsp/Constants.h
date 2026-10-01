#pragma once

/// All knob parameter constants in one place.
/// Edit here to change min/max/default values across the entire app.
namespace NAMConstants
{

// ---- Input / Output gain ------------------------------------------------
constexpr double K_GAIN_MIN = -20.0;
constexpr double K_GAIN_MAX = 20.0;
constexpr double K_GAIN_DEFAULT = 0.0;

// ---- EQ bands -----------------------------------------------------------
constexpr double K_EQ_BAND_MIN = -12.0;
constexpr double K_EQ_BAND_MAX = 12.0;
constexpr double K_EQ_BAND_DEFAULT = 0.0;

// ---- Noise Gate ---------------------------------------------------------
// Threshold reaches down to -100 dB so the knob's minimum is effectively
// "gate never closes"; -70 dB suits typical interface noise floors.
constexpr double K_GATE_THRESH_MIN = -100.0;
constexpr double K_GATE_THRESH_MAX = -20.0;
constexpr double K_GATE_THRESH_DEFAULT = -70.0;

// Fast attack so pick transients aren't clipped while the gate opens.
constexpr double K_GATE_ATTACK_MIN = 0.1;
constexpr double K_GATE_ATTACK_MAX = 50.0;
constexpr double K_GATE_ATTACK_DEFAULT = 1.0;

constexpr double K_GATE_RELEASE_MIN = 10.0;
constexpr double K_GATE_RELEASE_MAX = 1000.0;
constexpr double K_GATE_RELEASE_DEFAULT = 100.0;

// ---- Compressor ---------------------------------------------------------
constexpr double K_COMP_THRESH_MIN = -60.0;
constexpr double K_COMP_THRESH_MAX = 0.0;
constexpr double K_COMP_THRESH_DEFAULT = -20.0;

constexpr double K_COMP_RATIO_MIN = 1.0;
constexpr double K_COMP_RATIO_MAX = 20.0;
constexpr double K_COMP_RATIO_DEFAULT = 4.0;

constexpr double K_COMP_ATTACK_MIN = 0.1;
constexpr double K_COMP_ATTACK_MAX = 100.0;
constexpr double K_COMP_ATTACK_DEFAULT = 10.0;

constexpr double K_COMP_RELEASE_MIN = 10.0;
constexpr double K_COMP_RELEASE_MAX = 1000.0;
constexpr double K_COMP_RELEASE_DEFAULT = 100.0;

constexpr double K_COMP_MAKEUP_MIN = 0.0;
constexpr double K_COMP_MAKEUP_MAX = 24.0;
constexpr double K_COMP_MAKEUP_DEFAULT = 0.0;

// ---- Chorus -------------------------------------------------------------
constexpr double K_CHORUS_RATE_MIN = 0.1;
constexpr double K_CHORUS_RATE_MAX = 5.0;
constexpr double K_CHORUS_RATE_DEFAULT = 1.0;

constexpr double K_CHORUS_DEPTH_MIN = 0.0;
constexpr double K_CHORUS_DEPTH_MAX = 100.0;
constexpr double K_CHORUS_DEPTH_DEFAULT = 50.0;

constexpr double K_CHORUS_MIX_MIN = 0.0;
constexpr double K_CHORUS_MIX_MAX = 100.0;
constexpr double K_CHORUS_MIX_DEFAULT = 50.0;

// ---- Flanger ------------------------------------------------------------
// Flanging is a slow sweep; beyond ~2 Hz it turns into warbly vibrato.
constexpr double K_FLANGER_RATE_MIN = 0.05;
constexpr double K_FLANGER_RATE_MAX = 2.0;
constexpr double K_FLANGER_RATE_DEFAULT = 0.3;

constexpr double K_FLANGER_DEPTH_MIN = 0.0;
constexpr double K_FLANGER_DEPTH_MAX = 100.0;
constexpr double K_FLANGER_DEPTH_DEFAULT = 50.0;

constexpr double K_FLANGER_FEEDBACK_MIN = -99.0;
constexpr double K_FLANGER_FEEDBACK_MAX = 99.0;
constexpr double K_FLANGER_FEEDBACK_DEFAULT = 0.0;

constexpr double K_FLANGER_MIX_MIN = 0.0;
constexpr double K_FLANGER_MIX_MAX = 100.0;
constexpr double K_FLANGER_MIX_DEFAULT = 50.0;

// ---- Phaser -------------------------------------------------------------
constexpr double K_PHASER_RATE_MIN = 0.1;
constexpr double K_PHASER_RATE_MAX = 5.0;
constexpr double K_PHASER_RATE_DEFAULT = 1.0;

constexpr double K_PHASER_DEPTH_MIN = 0.0;
constexpr double K_PHASER_DEPTH_MAX = 100.0;
constexpr double K_PHASER_DEPTH_DEFAULT = 50.0;

constexpr double K_PHASER_FEEDBACK_MIN = 0.0;
constexpr double K_PHASER_FEEDBACK_MAX = 99.0;
constexpr double K_PHASER_FEEDBACK_DEFAULT = 50.0;

constexpr double K_PHASER_MIX_MIN = 0.0;
constexpr double K_PHASER_MIX_MAX = 100.0;
constexpr double K_PHASER_MIX_DEFAULT = 50.0;

// ---- Tremolo ------------------------------------------------------------
constexpr double K_TREMOLO_RATE_MIN = 0.5;
constexpr double K_TREMOLO_RATE_MAX = 15.0;
constexpr double K_TREMOLO_RATE_DEFAULT = 5.0;

constexpr double K_TREMOLO_DEPTH_MIN = 0.0;
constexpr double K_TREMOLO_DEPTH_MAX = 100.0;
constexpr double K_TREMOLO_DEPTH_DEFAULT = 50.0;

// ---- Delay --------------------------------------------------------------
// 20 ms floor keeps the delay line at least one sample long; 1 s covers
// musical delay times without drifting into looper territory.
constexpr double K_DELAY_TIME_MIN = 20.0;
constexpr double K_DELAY_TIME_MAX = 1000.0;
constexpr double K_DELAY_TIME_DEFAULT = 250.0;

constexpr double K_DELAY_FEEDBACK_MIN = 0.0;
constexpr double K_DELAY_FEEDBACK_MAX = 90.0;
constexpr double K_DELAY_FEEDBACK_DEFAULT = 40.0;

constexpr double K_DELAY_MIX_MIN = 0.0;
constexpr double K_DELAY_MIX_MAX = 100.0;
constexpr double K_DELAY_MIX_DEFAULT = 30.0;

// ---- Reverb -------------------------------------------------------------
constexpr double K_REVERB_ROOM_MIN = 0.0;
constexpr double K_REVERB_ROOM_MAX = 100.0;
constexpr double K_REVERB_ROOM_DEFAULT = 50.0;

constexpr double K_REVERB_DAMPING_MIN = 0.0;
constexpr double K_REVERB_DAMPING_MAX = 100.0;
constexpr double K_REVERB_DAMPING_DEFAULT = 50.0;

constexpr double K_REVERB_WIDTH_MIN = 0.0;
constexpr double K_REVERB_WIDTH_MAX = 100.0;
constexpr double K_REVERB_WIDTH_DEFAULT = 100.0;

constexpr double K_REVERB_MIX_MIN = 0.0;
constexpr double K_REVERB_MIX_MAX = 100.0;
constexpr double K_REVERB_MIX_DEFAULT = 30.0;

// ---- Dynamic Wah --------------------------------------------------------
constexpr double K_WAH_SENS_MIN = 0.0;
constexpr double K_WAH_SENS_MAX = 100.0;
constexpr double K_WAH_SENS_DEFAULT = 70.0;

constexpr double K_WAH_RES_MIN = 0.0;
constexpr double K_WAH_RES_MAX = 100.0;
constexpr double K_WAH_RES_DEFAULT = 60.0;

constexpr double K_WAH_MIX_MIN = 0.0;
constexpr double K_WAH_MIX_MAX = 100.0;
constexpr double K_WAH_MIX_DEFAULT = 100.0;

// ---- Drive (Phase 17) ---------------------------------------------------
constexpr double K_DRIVE_MIN = 0.0;
constexpr double K_DRIVE_MAX = 100.0;
constexpr double K_DRIVE_DEFAULT = 30.0;
constexpr double K_DRIVE_TONE_MIN = 0.0;
constexpr double K_DRIVE_TONE_MAX = 100.0;
constexpr double K_DRIVE_TONE_DEFAULT = 50.0;
constexpr double K_DRIVE_LEVEL_MIN = -20.0;
constexpr double K_DRIVE_LEVEL_MAX = 20.0;
constexpr double K_DRIVE_LEVEL_DEFAULT = 0.0;

// ---- Doubler ------------------------------------------------------------
constexpr double K_DOUBLER_SPREAD_MIN = 3.0;
constexpr double K_DOUBLER_SPREAD_MAX = 20.0;
constexpr double K_DOUBLER_SPREAD_DEFAULT = 10.0;
constexpr double K_DOUBLER_MIX_MIN = 0.0;
constexpr double K_DOUBLER_MIX_MAX = 100.0;
constexpr double K_DOUBLER_MIX_DEFAULT = 100.0;

// ---- Volume pedal -------------------------------------------------------
constexpr double K_VOLUME_POS_MIN = 0.0;
constexpr double K_VOLUME_POS_MAX = 100.0;
constexpr double K_VOLUME_POS_DEFAULT = 100.0;
constexpr double K_VOLUME_MID_MIN = -40.0;
constexpr double K_VOLUME_MID_MAX = 0.0;
constexpr double K_VOLUME_MID_DEFAULT = -12.0;

// ---- Phase 17: EQ frequencies + cuts ------------------------------------
constexpr double K_EQ_BASS_FREQ_MIN = 40.0, K_EQ_BASS_FREQ_MAX = 500.0,
                 K_EQ_BASS_FREQ_DEFAULT = 250.0;
constexpr double K_EQ_MID_FREQ_MIN = 200.0, K_EQ_MID_FREQ_MAX = 2500.0,
                 K_EQ_MID_FREQ_DEFAULT = 1000.0;
constexpr double K_EQ_HI_MID_FREQ_MIN = 800.0, K_EQ_HI_MID_FREQ_MAX = 8000.0,
                 K_EQ_HI_MID_FREQ_DEFAULT = 3000.0;
constexpr double K_EQ_TREBLE_FREQ_MIN = 1500.0, K_EQ_TREBLE_FREQ_MAX = 12000.0,
                 K_EQ_TREBLE_FREQ_DEFAULT = 4000.0;
constexpr double K_LOW_CUT_MIN = 20.0, K_LOW_CUT_MAX = 500.0, K_LOW_CUT_DEFAULT = 20.0; // 20 = off
constexpr double K_HIGH_CUT_MIN = 2000.0, K_HIGH_CUT_MAX = 20000.0,
                 K_HIGH_CUT_DEFAULT = 20000.0; // 20k = off

// ---- Phase 17: Delay ----------------------------------------------------
constexpr double K_DELAY_RIGHT_MIN = 25.0, K_DELAY_RIGHT_MAX = 200.0,
                 K_DELAY_RIGHT_DEFAULT = 100.0; // % of Time
constexpr double K_DELAY_CROSS_MIN = 0.0, K_DELAY_CROSS_MAX = 100.0,
                 K_DELAY_CROSS_DEFAULT = 100.0; // 100 = ping-pong
constexpr double K_DELAY_LOW_CUT_MAX = 1000.0;
constexpr double K_DELAY_HIGH_CUT_MIN = 1000.0;
constexpr double K_DELAY_SAT_MIN = 0.0, K_DELAY_SAT_MAX = 100.0, K_DELAY_SAT_DEFAULT = 0.0;
constexpr double K_DELAY_MOD_MIN = 0.0, K_DELAY_MOD_MAX = 100.0, K_DELAY_MOD_DEFAULT = 0.0;

// ---- Phase 17: Reverb ---------------------------------------------------
constexpr double K_REVERB_PRE_DELAY_MIN = 0.0, K_REVERB_PRE_DELAY_MAX = 250.0,
                 K_REVERB_PRE_DELAY_DEFAULT = 0.0;
constexpr double K_REVERB_LOW_CUT_MAX = 1000.0;
constexpr double K_REVERB_HIGH_CUT_MIN = 1000.0;

// ---- Phase 17: modulation modes / ranges ---------------------------------
constexpr double K_FLANGER_RANGE_MIN = 0.0, K_FLANGER_RANGE_MAX = 100.0,
                 K_FLANGER_RANGE_DEFAULT = 100.0;
constexpr double K_WAH_POSITION_MIN = 0.0, K_WAH_POSITION_MAX = 100.0,
                 K_WAH_POSITION_DEFAULT = 50.0;

} // namespace NAMConstants
