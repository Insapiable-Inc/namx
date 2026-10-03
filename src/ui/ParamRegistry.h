#pragma once

#include <vector>

#include <QString>

#include "Common.h"

namespace namx
{

class KnobWidget;
class ToggleSwitch;

// One user-facing parameter. The registry (built once by
// MainWindow::registerParameters) is the single source of truth for the
// parameter list: it drives widget→engine wiring, preset save/load
// (PresetSerializer), session persistence, and future MIDI mapping.
struct ParamDesc
{
    QString group; // JSON object holding the key; empty = top-level
    QString key;
    KnobWidget* knob = nullptr; // exactly one of knob/toggle is set
    ToggleSwitch* toggle = nullptr;
    double knob_default = 0.0; // applied when the key is absent from a present group
    bool toggle_default = false;
    // false = session only (tuner): not saved to presets, ignored when one loads.
    bool in_preset = true;
};

using ParamRegistry = vector<ParamDesc>;

} // namespace namx
