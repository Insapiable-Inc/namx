#pragma once

#include <QColor>
#include <QString>

#include "Common.h"

class QApplication;
class QWidget;

namespace npl
{

// Centralised palette and stylesheet for NAMPlayer — every color lives here
// (or in the stylesheet in Theme.cpp); widgets never hardcode hex values.
//
// Each effect card is tagged with its effect ID (same IDs as
// AudioEngine::effect_table, plus "neutral" for non-effect cards). The
// accent is rendered VERBATIM — stripe, title, knob ring/tick/value label,
// toggle "on" state all use the exact palette hex; contrast is managed by
// choosing the palette values, not by code. tagCard pushes the accent to
// the card's knobs/toggles explicitly because the app stylesheet disables
// parent→child palette propagation.
namespace Theme
{

// Accent for an effect ID ("gate", "nam", … or "neutral").
QColor accent(const QString& fx_id);
// Default accent before a card is tagged (also the VU-meter green).
QColor generic_accent();

// Neutral chrome tones for custom-painted widgets (knobs, switches).
QColor track(); // recessed track / switch off state
QColor track_hover(); // hovered track
QColor knob_body_top(); // knob body gradient, light end
QColor knob_body_bot(); // knob body gradient, dark end
QColor bezel(); // outline around the knob body
QColor well(); // recessed dial well
QColor dim_text(); // secondary labels (knob names, "Stereo", cents)
QColor thumb_top(); // switch thumb gradient, light end
QColor thumb_bot(); // switch thumb gradient, dark end

// VU meter level gradient, low → clip. The gradient is anchored to the full
// bar (not the filled chunk), so a color means an absolute level.
QColor meter_low();
QColor meter_mid();
QColor meter_high();
QColor meter_clip();

// Tuner display (LED ladder + seven-segment note).
QColor led_green(); // in tune
QColor led_red(); // off pitch
QColor display_bg(); // near-black LED panel

// Tags a card with its effect ID: sets the `fx` dynamic property (drives the
// per-effect QSS rules) and overrides QPalette::Highlight so descendant
// knobs/switches pick up the accent without individual tagging.
void tag_card(QWidget* card, const QString& fx_id);

// Install Fusion + a tooltip-only application stylesheet. Call once after
// QApplication is constructed.
void apply(QApplication& app);

// Apply the dark palette + the full stylesheet to the main window's subtree.
// Deliberately NOT application-wide: parentless dialogs (file dialogs,
// which are in-process Qt widgets even when "native" on KDE) stay outside
// this tree and keep the OS default look.
void apply_to(QWidget& root);

} // namespace Theme

} // namespace npl
