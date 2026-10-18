#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

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
// choosing the palette values, not by code. tag_card pushes the accent to
// the card's knobs/toggles, which paint it themselves.
namespace Theme
{

// A tooltip from file metadata or a file name: Qt renders anything that looks like
// HTML as rich text, so text that does is escaped and shown as-is (audit 2026-10-04,
// Fable item 24). Plain text is returned untouched (its line breaks stay).
QString plain_tooltip(const QString& text);

// Accent for an effect ID ("gate", "nam", … or "neutral"); a theme file may override it.
QColor accent(const QString& fx_id);
// The app accent: the system's highlight colour while following the system (Breeze
// blue otherwise): chrome, selections, and cards before they are tagged.
QColor generic_accent();

// Dark, light, or whichever the OS uses (the default; follows it live), or a theme file.
// The values are stored in `ui/theme`: append only.
enum class Mode
{
    SYSTEM,
    DARK,
    LIGHT,
    CUSTOM
};
Mode get_mode();
// Persists (`ui/theme`) and restyles the window at once. Not for Mode::CUSTOM: that
// takes a file, through set_custom.
void set_mode(Mode new_mode);
// Loads a theme file (ThemeFile.h) and switches to it; persists `ui/theme` and
// `ui/customTheme`. The file is watched: saving it restyles the window.
expected<void, QString> set_custom(const QString& path);
// The theme file in use (Mode::CUSTOM), else empty.
QString custom_path();
// The effect IDs a theme file may give an accent to.
QStringList effect_ids();
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

// Status colours (status bar dots and DSP load, MIDI light, count-in beats, the MIDI
// Learn glow): fine, warning, trouble. Readable on the status bar in every theme —
// darkened on a light one.
QColor status_ok();
QColor status_warn();
QColor status_bad();

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

// Node graph (ui/node-graph): the canvas and its cards.
// An effect's accent made readable on the current background (effect IDs, "merge",
// "neutral" = the app accent).
QColor fx_accent(const QString& fx_id);
QColor canvas_bg(); // behind the cards
QColor canvas_dot(); // the background grid
QColor node_fill(); // a card's body
QColor node_text();
QColor node_text_dim(); // its I/O line, port labels
QColor wire_drag(); // a wire being drawn

// Chilean flag ("Made in Chile" badge in the menu bar).
QColor flag_blue();
QColor flag_red();
QColor flag_white();

// Tags a card with its effect ID: sets the `fx` dynamic property (drives the
// per-effect QSS rules) and hands the accent to its knobs and switches.
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
