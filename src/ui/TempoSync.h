#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include <QString>

/// Note values for tempo-synced parameters (Phase 15). The engine stays
/// dumb: the GUI turns tempo × note value into the existing ms / Hz atomics
/// and re-pushes every synced parameter when the tempo changes.
///
/// The list is ordered longest → shortest so turning a sync knob clockwise
/// always speeds things up. Indices are stored in presets — append only,
/// never reorder.
namespace TempoSync
{

struct Note
{
    const char* label;
    double wholes; // length in whole notes
};

inline constexpr std::array<Note, 16> NOTES = {{
    {"4 bars", 4.0},
    {"2 bars", 2.0},
    {"1/1", 1.0},
    {"1/2.", 0.75},
    {"1/2", 0.5},
    {"1/4.", 0.375},
    {"1/2T", 1.0 / 3.0},
    {"1/4", 0.25},
    {"1/8.", 0.1875},
    {"1/4T", 1.0 / 6.0},
    {"1/8", 0.125},
    {"1/16.", 0.09375},
    {"1/8T", 1.0 / 12.0},
    {"1/16", 0.0625},
    {"1/16T", 1.0 / 24.0},
    {"1/32", 0.03125},
}};

inline constexpr double MIN_BPM = 40.0;
inline constexpr double MAX_BPM = 240.0;
inline constexpr double DEFAULT_BPM = 120.0;

// The knob's value (a double) rounded to a valid NOTES index.
inline size_t clamp_index(double v) {
    const long last = static_cast<long>(NOTES.size()) - 1;
    return static_cast<size_t>(std::clamp(std::lround(v), 0L, last));
}

inline QString label(double index) {
    return QString::fromLatin1(NOTES[clamp_index(index)].label);
}

/// Note length in milliseconds at `bpm` quarter notes per minute.
inline double note_ms(double bpm, double index) {
    return NOTES[clamp_index(index)].wholes * 4.0 * 60000.0 / bpm;
}

/// LFO rate whose period is one note.
inline double note_hz(double bpm, double index) {
    return 1000.0 / note_ms(bpm, index);
}

} // namespace TempoSync
