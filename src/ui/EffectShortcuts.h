#pragma once

#include <vector>

class QShortcut;
class QWidget;
class ToggleSwitch;

/// Assigns keyboard shortcuts to the effect strip toggles, left to right,
/// following the digit row of a Latin American keyboard layout:
///
///   1 2 3 … 9 0 ' ¿ Backspace   enable/bypass toggle of each effect card
///   Q W E …                     stereo toggle, in order, for the cards that have one
///
/// Register cards with addEffect() in strip (chain) order; keys are handed
/// out sequentially, and cards beyond the end of a key list get no shortcut.
/// When the strip is reordered, call clear() and re-register so the digit row
/// keeps matching the visual left-to-right order.
class EffectShortcuts
{
public:
    explicit EffectShortcuts(QWidget* new_window)
        : window(new_window) {
    }

    /// Register one effect card. `stereo` is null for cards without a stereo toggle.
    void add_effect(ToggleSwitch* enable, ToggleSwitch* stereo = nullptr);

    /// Delete all created shortcuts and restart key assignment (chain reorder).
    void clear();

private:
    void bind(ToggleSwitch* toggle, int key, const char* action);

    QWidget* window;
    int enable_index{0};
    int stereo_index{0};
    std::vector<QShortcut*> shortcuts;
};
