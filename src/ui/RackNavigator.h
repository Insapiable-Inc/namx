#pragma once

#include <QObject>
#include <functional>
#include <vector>

class EffectBase;
class KnobWidget;
class QScrollArea;
class QWidget;
class ToggleSwitch;

/// Keyboard cursor over the rack: one row (Input, the effect cards in chain
/// order, Output) and one knob within it. Arrows only ever move the cursor;
/// value and toggle keys act on whatever it points at. Independent of Qt
/// focus — dials are NoFocus and the navigator filters key presses at the
/// application level, so the scheme keeps working whatever widget happens
/// to hold focus, while text fields, menus and dialogs are left alone.
///
///   ↑ / ↓        previous / next row (keeps the knob column; wraps)
///   ← / →        previous / next knob in the row
///   + / -        nudge the knob by its step; Shift = fine (×0.1), Ctrl = coarse (×10)
///   Home / End   knob to minimum / maximum
///   Delete       knob back to its default
///   Space        toggle the row on/off
///   S            toggle the row's stereo mode (rows that have one)
///   Enter        row action (Amplifier/Cabinet: Load…); on a knob: type a value
///   Esc          hide the cursor
///
/// The cursor is invisible until the first key press (mouse users never see
/// it); clicking a knob moves the cursor there silently.
class RackNavigator : public QObject
{
    Q_OBJECT
public:
    struct Row
    {
        QWidget*                 card     = nullptr;
        EffectBase*              effect   = nullptr; // null for Input/Output
        std::vector<KnobWidget*> knobs;
        ToggleSwitch*            enable   = nullptr;
        ToggleSwitch*            stereo   = nullptr;
        std::function<void()>    activate;           // Enter on the row (optional)
    };

    RackNavigator(QWidget* window, QScrollArea* strip, QObject* parent = nullptr);

    /// Replace the row list (strip rebuilt / reordered). The cursor stays on
    /// the same card when it is still present, else on the first row.
    void setRows(std::vector<Row> rows);

    bool isVisible() const { return mVisible; }
    void hide();

    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    bool handleKey(class QKeyEvent* ke);
    bool keysApplyNow() const;
    void show();
    void moveRow(int delta);
    void moveKnob(int delta);
    void applyHighlight(int row, int knob, bool on);
    void setCursor(int row, int knob);
    void ensureVisible();
    KnobWidget* currentKnob() const;
    Row*        currentRow();
    void typeValue(KnobWidget* knob);

    QWidget*         mWindow;
    QScrollArea*     mStrip;
    std::vector<Row> mRows;
    int  mRow{0};
    int  mKnob{0};
    int  mPreferredKnob{0}; // column kept while passing through knob-less rows
    bool mVisible{false};
};
