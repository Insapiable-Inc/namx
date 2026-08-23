#pragma once

#include <QColor>
#include <QString>

class QApplication;
class QWidget;

/// Centralised palette and stylesheet for NAMPlayer — every color lives here
/// (or in the stylesheet in Theme.cpp); widgets never hardcode hex values.
///
/// Each effect card is tagged with its effect ID (same IDs as
/// AudioEngine::mEffectTable, plus "neutral" for non-effect cards). The
/// accent is rendered VERBATIM — stripe, title, knob ring/tick/value label,
/// toggle "on" state all use the exact palette hex; contrast is managed by
/// choosing the palette values, not by code. tagCard pushes the accent to
/// the card's knobs/toggles explicitly because the app stylesheet disables
/// parent→child palette propagation.
namespace Theme {

/// Accent for an effect ID ("gate", "nam", … or "neutral").
QColor accent(const QString& fxId);
/// Default accent before a card is tagged (also the VU-meter green).
QColor genericAccent();

// Neutral chrome tones for custom-painted widgets (knobs, switches).
QColor track();        // recessed track / switch off state
QColor trackHover();   // hovered track
QColor knobBodyTop();  // knob body gradient, light end
QColor knobBodyBot();  // knob body gradient, dark end
QColor bezel();        // outline around the knob body
QColor well();         // recessed dial well
QColor dimText();      // secondary labels (knob names, "Stereo", cents)
QColor thumbTop();     // switch thumb gradient, light end
QColor thumbBot();     // switch thumb gradient, dark end

// VU meter level gradient, low → clip. The gradient is anchored to the full
// bar (not the filled chunk), so a color means an absolute level.
QColor meterLow();
QColor meterMid();
QColor meterHigh();
QColor meterClip();

/// Tags a card with its effect ID: sets the `fx` dynamic property (drives the
/// per-effect QSS rules) and overrides QPalette::Highlight so descendant
/// knobs/switches pick up the accent without individual tagging.
void tagCard(QWidget* card, const QString& fxId);

/// Install Fusion + the global dark palette + the application stylesheet.
/// Call once after QApplication is constructed.
void apply(QApplication& app);

} // namespace Theme
