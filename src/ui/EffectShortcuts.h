#pragma once

#include <QShortcut>
#include <QWidget>

#include "Common.h"
#include "ToggleSwitch.h"

namespace npl
{

// Keyboard shortcuts for the effects' on/off switches, by position in the strip:
//
//   1 2 3 … 9 0   then   Q W E R T Y U I O P
//
// Register the cards with add_effect() in strip order; keys are handed out in that
// order, and cards past the last key get none. After a reorder, clear() and
// register again so the keys keep following the rows top to bottom. Plain keys on
// purpose (practice at the desk); live switching is what MIDI mappings are for.
class EffectShortcuts
{
public:
    explicit EffectShortcuts(QWidget* new_window)
        : window(new_window) {
    }

    void add_effect(ToggleSwitch* enable);
    // Deletes the shortcuts and restarts the key assignment.
    void clear();

private:
    QWidget* window;
    size_t next_key = 0;
    vector<QShortcut*> shortcuts;
};

} // namespace npl
