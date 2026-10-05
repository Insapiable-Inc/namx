#pragma once

#include <QColor>
#include <QString>

#include "Common.h"

class QApplication;
class QWidget;

namespace namx
{

// Centralised palette and stylesheet for NAM-X — every color lives here
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

// A tooltip from file metadata or a file name: Qt renders anything that looks like
// HTML as rich text, so text that does is escaped and shown as-is (audit 2026-10-04,
// Fable item 24). Plain text is returned untouched (its line breaks stay).
QString plain_tooltip(const QString& text);

// Accent for an effect ID ("gate", "nam", … or "neutral").
QColor accent(const QString& fx_id);
// The app accent: the system's highlight colour while following the system (Breeze
// blue otherwise): chrome, selections, and cards before they are tagged.
QColor generic_accent();

// Dark, light, or whichever the OS uses (the default; follows it live).
enum class Mode
{
    SYSTEM,
    DARK,
    LIGHT
};
Mode get_mode();
// Persists (`ui/theme`) and restyles the window at once.
void set_mode(Mode new_mode);
// Re-reads the system scheme and accent and restyles the window; without `force`,
// only when one of them changed.
void reapply(bool force = false);

// Neutral chrome tones for custom-painted widgets (knobs, switches).
QColor track(); // recessed track / switch off state
QColor track_hover(); // hovered track
QColor knob_body_top(); // knob body gradient, light end
QColor knob_body_bot(); // knob body gradient, dark end
QColor bezel(); // outline around the knob body
QColor detent(); // a stepped knob's unlit position dots
QColor well(); // recessed dial well
QColor dim_text(); // secondary labels (knob names, "Stereo", cents)
QColor thumb_top(); // switch thumb gradient, light end
QColor thumb_bot(); // switch thumb gradient, dark end
QColor thumb_outline(); // switch thumb edge; invalid = none
int switch_rim_alpha(bool checked); // the switch track's dark edge

// VU meter level gradient, low → clip. The gradient is anchored to the full
// bar (not the filled chunk), so a color means an absolute level.
QColor meter_low();
QColor meter_mid();
QColor meter_high();
QColor meter_clip();

// Tuner display (LED ladder + seven-segment note).
QColor led_green(); // in tune, the meters' safe zone
QColor led_red(); // off pitch
QColor display_bg(); // near-black LED panel
QColor display_line(); // the LED panels' frame
// Dim text and unlit digits on an LED panel: the panel's faint tone moved away from the
// panel by `factor` (lighter on a dark panel, darker on a light one).
QColor display_faint(int factor);
// An unlit LED's tint alpha: `base_alpha` on a dark panel, stronger on a light one.
int unlit_alpha(int base_alpha);
QColor led_amber(); // the meters' hot zone

// Chilean flag ("Made in Chile" badge in the menu bar).
QColor flag_blue();
QColor flag_red();
QColor flag_white();

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

} // namespace namx
