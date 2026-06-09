#pragma once

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
/// The created QShortcuts are parented to the window, so this object itself
/// may be discarded once registration is done.
class EffectShortcuts
{
public:
    explicit EffectShortcuts(QWidget* window) : mWindow(window) {}

    /// Register one effect card. `stereo` is null for cards without a stereo toggle.
    void addEffect(ToggleSwitch* enable, ToggleSwitch* stereo = nullptr);

private:
    void bind(ToggleSwitch* toggle, int key, const char* action);

    QWidget* mWindow;
    int mEnableIndex{0};
    int mStereoIndex{0};
};
