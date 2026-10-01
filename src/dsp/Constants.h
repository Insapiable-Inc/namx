#pragma once

/// All knob parameter constants in one place.
/// Edit here to change min/max/default values across the entire app.
namespace NAMConstants {

// ---- Input / Output gain ------------------------------------------------
constexpr double kGainMin     = -20.0;
constexpr double kGainMax     =  20.0;
constexpr double kGainDefault =   0.0;

// ---- EQ bands -----------------------------------------------------------
constexpr double kEQBandMin     = -12.0;
constexpr double kEQBandMax     =  12.0;
constexpr double kEQBandDefault =   0.0;

// ---- Noise Gate ---------------------------------------------------------
// Threshold reaches down to -100 dB so the knob's minimum is effectively
// "gate never closes"; -70 dB suits typical interface noise floors.
constexpr double kGateThreshMin     = -100.0;
constexpr double kGateThreshMax     =  -20.0;
constexpr double kGateThreshDefault =  -70.0;

// Fast attack so pick transients aren't clipped while the gate opens.
constexpr double kGateAttackMin     =  0.1;
constexpr double kGateAttackMax     = 50.0;
constexpr double kGateAttackDefault =  1.0;

constexpr double kGateReleaseMin     =   10.0;
constexpr double kGateReleaseMax     = 1000.0;
constexpr double kGateReleaseDefault =  100.0;

// ---- Compressor ---------------------------------------------------------
constexpr double kCompThreshMin     = -60.0;
constexpr double kCompThreshMax     =   0.0;
constexpr double kCompThreshDefault = -20.0;

constexpr double kCompRatioMin     =  1.0;
constexpr double kCompRatioMax     = 20.0;
constexpr double kCompRatioDefault =  4.0;

constexpr double kCompAttackMin     =    0.1;
constexpr double kCompAttackMax     =  100.0;
constexpr double kCompAttackDefault =   10.0;

constexpr double kCompReleaseMin     =   10.0;
constexpr double kCompReleaseMax     = 1000.0;
constexpr double kCompReleaseDefault =  100.0;

constexpr double kCompMakeupMin     =  0.0;
constexpr double kCompMakeupMax     = 24.0;
constexpr double kCompMakeupDefault =  0.0;

// ---- Chorus -------------------------------------------------------------
constexpr double kChorusRateMin     = 0.1;
constexpr double kChorusRateMax     = 5.0;
constexpr double kChorusRateDefault = 1.0;

constexpr double kChorusDepthMin     =  0.0;
constexpr double kChorusDepthMax     = 100.0;
constexpr double kChorusDepthDefault =  50.0;

constexpr double kChorusMixMin     =  0.0;
constexpr double kChorusMixMax     = 100.0;
constexpr double kChorusMixDefault =  50.0;

// ---- Flanger ------------------------------------------------------------
// Flanging is a slow sweep; beyond ~2 Hz it turns into warbly vibrato.
constexpr double kFlangerRateMin     = 0.05;
constexpr double kFlangerRateMax     = 2.0;
constexpr double kFlangerRateDefault = 0.3;

constexpr double kFlangerDepthMin     =  0.0;
constexpr double kFlangerDepthMax     = 100.0;
constexpr double kFlangerDepthDefault =  50.0;

constexpr double kFlangerFeedbackMin     = -99.0;
constexpr double kFlangerFeedbackMax     =  99.0;
constexpr double kFlangerFeedbackDefault =   0.0;

constexpr double kFlangerMixMin     =  0.0;
constexpr double kFlangerMixMax     = 100.0;
constexpr double kFlangerMixDefault =  50.0;

// ---- Phaser -------------------------------------------------------------
constexpr double kPhaserRateMin     = 0.1;
constexpr double kPhaserRateMax     = 5.0;
constexpr double kPhaserRateDefault = 1.0;

constexpr double kPhaserDepthMin     =  0.0;
constexpr double kPhaserDepthMax     = 100.0;
constexpr double kPhaserDepthDefault =  50.0;

constexpr double kPhaserFeedbackMin     =  0.0;
constexpr double kPhaserFeedbackMax     = 99.0;
constexpr double kPhaserFeedbackDefault = 50.0;

constexpr double kPhaserMixMin     =  0.0;
constexpr double kPhaserMixMax     = 100.0;
constexpr double kPhaserMixDefault =  50.0;

// ---- Tremolo ------------------------------------------------------------
constexpr double kTremoloRateMin     =  0.5;
constexpr double kTremoloRateMax     = 15.0;
constexpr double kTremoloRateDefault =  5.0;

constexpr double kTremoloDepthMin     =  0.0;
constexpr double kTremoloDepthMax     = 100.0;
constexpr double kTremoloDepthDefault =  50.0;

// ---- Delay --------------------------------------------------------------
// 20 ms floor keeps the delay line at least one sample long; 1 s covers
// musical delay times without drifting into looper territory.
constexpr double kDelayTimeMin     =   20.0;
constexpr double kDelayTimeMax     = 1000.0;
constexpr double kDelayTimeDefault =  250.0;

constexpr double kDelayFeedbackMin     =  0.0;
constexpr double kDelayFeedbackMax     = 90.0;
constexpr double kDelayFeedbackDefault = 40.0;

constexpr double kDelayMixMin     =  0.0;
constexpr double kDelayMixMax     = 100.0;
constexpr double kDelayMixDefault =  30.0;

// ---- Reverb -------------------------------------------------------------
constexpr double kReverbRoomMin     =  0.0;
constexpr double kReverbRoomMax     = 100.0;
constexpr double kReverbRoomDefault =  50.0;

constexpr double kReverbDampingMin     =  0.0;
constexpr double kReverbDampingMax     = 100.0;
constexpr double kReverbDampingDefault =  50.0;

constexpr double kReverbWidthMin     =  0.0;
constexpr double kReverbWidthMax     = 100.0;
constexpr double kReverbWidthDefault = 100.0;

constexpr double kReverbMixMin     =  0.0;
constexpr double kReverbMixMax     = 100.0;
constexpr double kReverbMixDefault =  30.0;

// ---- Dynamic Wah --------------------------------------------------------
constexpr double kWahSensMin     =  0.0;
constexpr double kWahSensMax     = 100.0;
constexpr double kWahSensDefault =  70.0;

constexpr double kWahResMin     =  0.0;
constexpr double kWahResMax     = 100.0;
constexpr double kWahResDefault =  60.0;

constexpr double kWahMixMin     =  0.0;
constexpr double kWahMixMax     = 100.0;
constexpr double kWahMixDefault = 100.0;

// ---- Drive (Phase 17) ---------------------------------------------------
constexpr double kDriveMin     =   0.0;
constexpr double kDriveMax     = 100.0;
constexpr double kDriveDefault =  30.0;
constexpr double kDriveToneMin     =   0.0;
constexpr double kDriveToneMax     = 100.0;
constexpr double kDriveToneDefault =  50.0;
constexpr double kDriveLevelMin     = -20.0;
constexpr double kDriveLevelMax     =  20.0;
constexpr double kDriveLevelDefault =   0.0;

// ---- Doubler ------------------------------------------------------------
constexpr double kDoublerSpreadMin     =  3.0;
constexpr double kDoublerSpreadMax     = 20.0;
constexpr double kDoublerSpreadDefault = 10.0;
constexpr double kDoublerMixMin     =   0.0;
constexpr double kDoublerMixMax     = 100.0;
constexpr double kDoublerMixDefault = 100.0;

// ---- Volume pedal -------------------------------------------------------
constexpr double kVolumePosMin     =   0.0;
constexpr double kVolumePosMax     = 100.0;
constexpr double kVolumePosDefault = 100.0;
constexpr double kVolumeMidMin     = -40.0;
constexpr double kVolumeMidMax     =   0.0;
constexpr double kVolumeMidDefault = -12.0;

} // namespace NAMConstants
