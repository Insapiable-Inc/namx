#pragma once

/// GUI behaviour and layout constants.
/// Edit here to adjust sizing, timers, and scroll increments across the entire UI.
namespace GUI {

// ---- Main window --------------------------------------------------------
constexpr int kWindowMinWidth  = 1400;
constexpr int kWindowMinHeight =  500;

// ---- Root layout --------------------------------------------------------
constexpr int kRootMarginH   = 8;
constexpr int kRootMarginTop = 4;
constexpr int kRootMarginBot = 6;
constexpr int kRootSpacing   = 4;

// ---- Effect strip layout ------------------------------------------------
constexpr int kStripMargin  = 2;   // strip widget margin (all sides)
constexpr int kStripSpacing = 4;
constexpr int kColMinWidth  = 90;
constexpr int kColMarginH   = 4;   // left/right margin inside a column GroupBox
constexpr int kColMarginTop = 6;   // top margin (leaves room under the GroupBox title)
constexpr int kColMarginBot = 4;
constexpr int kColSpacing   = 2;   // tight spacing used by most effect columns
constexpr int kColSpacingLoose = 3; // slightly looser spacing (tuner column)
constexpr int kColSpacingWide  = 4; // wide spacing (amp / IR / output columns)
constexpr int kBoxMargin    = 4;   // NAM / IR file-row box margins (all sides)

// ---- Font size boosts ---------------------------------------------------
constexpr int kTitleFontBoost     = 4;   // pt added to the app title label
constexpr int kTunerNoteFontBoost = 7;   // pt added to the tuner note label

// ---- VU meter -----------------------------------------------------------
constexpr int   kMeterWidth   = 14;    // px
constexpr int   kMeterMinH    = 70;    // px
constexpr float kMeterDBFloor = -60.0f;
constexpr float kMeterDBRange =  60.0f;   // ceiling (0 dB) minus floor span

// ---- Tuner display ------------------------------------------------------
constexpr int kTunerBarWidth   = 14;   // px
constexpr int kTunerBarMinH    = 60;   // px
constexpr int kTunerBarCenter  = 50;   // bar value when perfectly in-tune
constexpr int kTunerNoteWidth  = 52;   // px — minimum width of note label
constexpr int kTunerCentsWidth = 36;   // px — minimum width of cents label

// ---- Timer intervals (ms) -----------------------------------------------
constexpr int kMeterTimerMs = 50;
constexpr int kTunerTimerMs = 100;

// ---- Knob widget geometry -----------------------------------------------
constexpr int kKnobSizeW         = 72;
constexpr int kKnobSizeH         = 88;
constexpr int kKnobMinSizeW      = 60;
constexpr int kKnobMinSizeH      = 76;
constexpr int kKnobMargin        = 2;   // layout margin inside the knob widget
constexpr int kKnobSpacing       = 1;
constexpr int kKnobValueDecimals = 1;   // decimal places shown in the value label

// ---- Knob drag & scroll -------------------------------------------------
constexpr int kKnobDragPixels        = 200;   // px of vertical drag = full range sweep
constexpr int kKnobScrollFallbackPct = 100;   // divisor for default scroll (1 % of range)

// ---- Toggle switch geometry ---------------------------------------------
constexpr int kToggleW           = 42;   // px
constexpr int kToggleH           = 22;   // px
constexpr int kToggleThumbMargin = 2;    // px gap between thumb and track edge
constexpr int kToggleHoverPct    = 130;  // lighter() factor for off-state hover

// ---- Scroll-wheel increments (value units per standard wheel tick) ------
constexpr double kScrollGain      = 0.1;   // dB  — input / output gain
constexpr double kScrollEQBand    = 0.1;   // dB  — EQ low / mid / treble
constexpr double kScrollThreshdB  = 1.0;   // dB  — gate & comp threshold
constexpr double kScrollMakeup    = 0.5;   // dB  — compressor makeup gain
constexpr double kScrollRatio     = 0.5;   // :1  — compressor ratio
constexpr double kScrollAttackMs  = 1.0;   // ms  — gate & comp attack
constexpr double kScrollReleaseMs = 10.0;  // ms  — gate & comp release
constexpr double kScrollSemitones = 1.0;   // st  — pitch shift semitones
constexpr double kScrollPercent   = 1.0;   // %   — depth / mix / feedback
constexpr double kScrollRateHz    = 0.1;   // Hz  — LFO rate (all effects)
constexpr double kScrollDelayMs   = 1.0;   // ms  — delay time

} // namespace GUI
