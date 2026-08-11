#pragma once

#include <QString>
#include <vector>

class KnobWidget;
class ToggleSwitch;

/// One user-facing parameter. The registry (built once by
/// MainWindow::registerParameters) is the single source of truth for the
/// parameter list: it drives widget→engine wiring, preset save/load
/// (PresetSerializer), session persistence, and future MIDI mapping.
struct ParamDesc
{
    QString group;                    // JSON object holding the key; empty = top-level
    QString key;
    KnobWidget*   knob   = nullptr;   // exactly one of knob/toggle is set
    ToggleSwitch* toggle = nullptr;
    double knobDefault   = 0.0;       // applied when the key is absent from a present group
    bool   toggleDefault = false;
};

using ParamRegistry = std::vector<ParamDesc>;
