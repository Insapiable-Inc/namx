#pragma once

#include <QColor>

class QApplication;
class QWidget;

/// Centralised palette and stylesheet for NAMPlayer.
/// Categories drive both the top-edge accent on each effect card and the
/// child widgets' QPalette::Highlight (which feeds knob value arcs, toggle
/// switches, and the value label color).
namespace Theme {

enum class Category {
    Neutral,   // input, output, tuner
    Dynamics,  // gate, comp
    Filter,    // dynamic wah
    ModPre,    // flanger, phaser, chorus1
    Amp,       // NAM
    EQ,
    ModPost,   // chorus2, tremolo
    Time,      // delay, reverb
    Cab,       // IR
};

const char* categoryName(Category c);
QColor      categoryColor(Category c);

/// Marks a widget (typically a QGroupBox) as belonging to a category:
///   * sets the `category` dynamic property (drives the top-edge accent in QSS)
///   * overrides QPalette::Highlight so descendant knobs/switches pick up the
///     same accent without each having to be tagged individually.
void tagCard(QWidget* card, Category c);

/// Install Fusion + the global dark palette + the application stylesheet.
/// Call once after QApplication is constructed.
void apply(QApplication& app);

} // namespace Theme
