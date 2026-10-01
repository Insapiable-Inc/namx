#pragma once

#include <QString>

#include <algorithm>
#include <array>
#include <cmath>

/// Note values for tempo-synced parameters (Phase 15). The engine stays
/// dumb: the GUI turns tempo × note value into the existing ms / Hz atomics
/// and re-pushes every synced parameter when the tempo changes.
///
/// The list is ordered longest → shortest so turning a sync knob clockwise
/// always speeds things up. Indices are stored in presets — append only,
/// never reorder.
namespace TempoSync {

struct Note
{
    const char* label;
    double      wholes; // length in whole notes
};

inline constexpr std::array<Note, 16> kNotes = {{
    {"4 bars", 4.0},       {"2 bars", 2.0},         {"1/1", 1.0},         {"1/2.", 0.75},
    {"1/2", 0.5},          {"1/4.", 0.375},         {"1/2T", 1.0 / 3.0},  {"1/4", 0.25},
    {"1/8.", 0.1875},      {"1/4T", 1.0 / 6.0},     {"1/8", 0.125},       {"1/16.", 0.09375},
    {"1/8T", 1.0 / 12.0},  {"1/16", 0.0625},        {"1/16T", 1.0 / 24.0}, {"1/32", 0.03125},
}};

inline constexpr double kMinBpm = 40.0;
inline constexpr double kMaxBpm = 240.0;
inline constexpr double kDefaultBpm = 120.0;

inline int clampIndex(double v)
{
    return std::clamp(static_cast<int>(std::lround(v)), 0, static_cast<int>(kNotes.size()) - 1);
}

inline QString label(double index) { return QString::fromLatin1(kNotes[clampIndex(index)].label); }

/// Note length in milliseconds at `bpm` quarter notes per minute.
inline double noteMs(double bpm, double index)
{
    return kNotes[clampIndex(index)].wholes * 4.0 * 60000.0 / bpm;
}

/// LFO rate whose period is one note.
inline double noteHz(double bpm, double index) { return 1000.0 / noteMs(bpm, index); }

} // namespace TempoSync
