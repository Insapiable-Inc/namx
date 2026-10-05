#include "Theme.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QEvent>
#include <QPalette>
#include <QPointer>
#include <QSettings>
#include <QStyle>
#include <QStyleHints>
#include <QTextDocument> // Qt::mightBeRichText
#include <QTimer>
#include <QVariant>
#include <QWidget>

#include "Common.h"
#include "KnobWidget.h"
#include "ToggleSwitch.h"

namespace namx
{

namespace Theme
{

QString plain_tooltip(const QString& text) {
    if (!Qt::mightBeRichText(text)) {
        return text;
    }
    return "<span>" + text.toHtmlEscaped().replace('\n', "<br>") + "</span>";
}

// Per-effect accents (user-chosen, 2026-08-10). IDs match
// AudioEngine::effect_table; "neutral" covers Input/Output and fallbacks.
struct FxColor
{
    const char* id;
    const char* accent;
};
static constexpr FxColor FX_COLORS[] = {
    {"tuner", "#b6aa8d"},   {"gate", "#8f969e"},    {"comp", "#4f78bd"},   {"wah", "#d4c19a"},
    {"flanger", "#93477b"}, {"phaser", "#4bba5b"},  {"nam", "#e4b954"},    {"eq", "#a5a38f"},
    {"chorus", "#8dccf3"},  {"tremolo", "#19848e"}, {"delay", "#aaa6a0"},  {"reverb", "#8f7d99"},
    {"ir", "#ef661a"},      {"drive", "#2e9a5c"},   {"volume", "#9fb4c7"}, {"transpose", "#c9a6f2"},
    {"neutral", "#7d8590"},
};

QColor accent(const QString& fx_id) {
    for (const auto& fc : FX_COLORS) {
        if (fx_id == QLatin1String(fc.id)) {
            return QColor(fc.accent);
        }
    }
    return QColor("#7d8590");
}

// Chrome tones (stylesheet roles + custom-painted widgets), as hex tables for the forced
// Dark / Light themes. The per-effect
// accents, the meters and the LED displays are the same in both: the displays are
// dark panels on purpose, like hardware. DARK is the approved palette, verbatim.
struct PaletteHex
{
    const char* window; // main window background
    const char* surface; // bars, menus, lists
    const char* card; // effect cards
    const char* raised; // tool buttons, tooltips
    const char* control; // push buttons, combo boxes
    const char* control_hover;
    const char* handle_hover; // scroll handles, hovered
    const char* selection; // hovered / selected menu and list items
    const char* pressed;
    const char* line; // separators and quiet borders
    const char* border; // control borders
    const char* border_hover;
    const char* disabled_bg;
    const char* disabled_border;
    const char* disabled_text;
    const char* text;
    const char* text_dim; // secondary labels
    const char* text_muted; // status and captions
    const char* text_faint; // grips, captions
    const char* text_off; // empty preset slots
    const char* notice; // status bar problems
    const char* shadow;
    // Custom-painted widgets (knobs, switches, fields).
    const char* track;
    const char* track_hover;
    const char* knob_body_top;
    const char* knob_body_bot;
    const char* bezel;
    const char* detent;
    const char* well;
    const char* thumb_top;
    const char* thumb_bot;
    const char* thumb_outline; // "" = none (the dark theme's thumb needs no edge)
    int switch_rim_off; // alpha of the switch track's dark edge, off / on
    int switch_rim_on;
    // LED displays (tuner, level meters).
    const char* display_bg;
    const char* display_line; // frame
    const char* display_faint; // base of the dim text and unlit digits
    const char* led_green;
    const char* led_red;
    const char* led_amber; // the meters' hot zone
    int led_unlit_scale; // unlit segments' tint strength (alpha multiplier)
};

// The tones in use: one of the tables, or derived from the OS palette (Mode::SYSTEM).
struct Colors
{
    QColor window; // main window background
    QColor surface; // bars, menus, lists
    QColor card; // effect cards
    QColor raised; // tool buttons, tooltips
    QColor control; // push buttons, combo boxes
    QColor control_hover;
    QColor handle_hover; // scroll handles, hovered
    QColor selection; // hovered / selected menu and list items
    QColor pressed;
    QColor line; // separators and quiet borders
    QColor border; // control borders
    QColor border_hover;
    QColor disabled_bg;
    QColor disabled_border;
    QColor disabled_text;
    QColor text;
    QColor text_dim; // secondary labels
    QColor text_muted; // status and captions
    QColor text_faint; // grips, captions
    QColor text_off; // empty preset slots
    QColor notice; // status bar problems
    QColor shadow;
    // Custom-painted widgets (knobs, switches, fields).
    QColor track;
    QColor track_hover;
    QColor knob_body_top;
    QColor knob_body_bot;
    QColor bezel;
    QColor detent;
    QColor well;
    QColor thumb_top;
    QColor thumb_bot;
    QColor thumb_outline; // invalid = none (the dark theme's thumb needs no edge)
    int switch_rim_off; // alpha of the switch track's dark edge, off / on
    int switch_rim_on;
    // LED displays (tuner, level meters).
    QColor display_bg;
    QColor display_line; // frame
    QColor display_faint; // base of the dim text and unlit digits
    QColor led_green;
    QColor led_red;
    QColor led_amber; // the meters' hot zone
    int led_unlit_scale; // unlit segments' tint strength (alpha multiplier)
};

static constexpr PaletteHex DARK = {
    "#15171c", "#1a1d24", "#1f242c", "#232830", "#2a2f38", "#353c4a", "#424b5c",
    "#2f3744", "#1f242c", "#2a2f38", "#353c4a", "#424b5c", "#1d2027", "#25292f",
    "#555c6b", "#d8dee9", "#aab0bc", "#9aa1ad", "#7d8590", "#6b7280", "#ff5252",
    "#0a0c10", "#2a2f38", "#353c4a", "#2e3441", "#1c2028", "#0e1015", "#6b7280",
    "#181b22", "#ffffff", "#d8dee9", "",        100,       60,        "#07080c",
    "#2a2f38", "#2a2f38", "#42e86c", "#ff453a", "#f0c850", 1,
};

// Draft (2026-10-04, experiment/system-theme): neutral cool greys around Breeze Light;
// to be tuned by eye like the dark one.
static constexpr PaletteHex LIGHT = {
    "#eceef1", "#f6f7f9", "#ffffff", "#f1f3f5", "#e3e6ea", "#d6dae0", "#b4bac4",
    "#dde4ee", "#cdd2d9", "#d5d9df", "#c3c8d0", "#a9b0ba", "#eef0f2", "#dde0e4",
    "#a3a9b2", "#1f2328", "#4f5661", "#5f6670", "#7d8590", "#9aa1ab", "#c62828",
    "#9aa1ab", "#cfd4da", "#c2c8d0", "#ffffff", "#f0f2f5", "#aeb5bf", "#9aa1ab",
    "#e6e9ed", "#ffffff", "#f3f5f8", "#b4bac4", 40,        30,        "#dfe3e8",
    "#c3c8d0", "#8a919b", "#16a34a", "#dc2626", "#d19a00", 2,
};

static Colors from_hex(const PaletteHex& t) {
    return Colors{
        QColor(t.window),        QColor(t.surface),
        QColor(t.card),          QColor(t.raised),
        QColor(t.control),       QColor(t.control_hover),
        QColor(t.handle_hover),  QColor(t.selection),
        QColor(t.pressed),       QColor(t.line),
        QColor(t.border),        QColor(t.border_hover),
        QColor(t.disabled_bg),   QColor(t.disabled_border),
        QColor(t.disabled_text), QColor(t.text),
        QColor(t.text_dim),      QColor(t.text_muted),
        QColor(t.text_faint),    QColor(t.text_off),
        QColor(t.notice),        QColor(t.shadow),
        QColor(t.track),         QColor(t.track_hover),
        QColor(t.knob_body_top), QColor(t.knob_body_bot),
        QColor(t.bezel),         QColor(t.detent),
        QColor(t.well),          QColor(t.thumb_top),
        QColor(t.thumb_bot),     QColor(t.thumb_outline),
        t.switch_rim_off,        t.switch_rim_on,
        QColor(t.display_bg),    QColor(t.display_line),
        QColor(t.display_faint), QColor(t.led_green),
        QColor(t.led_red),       QColor(t.led_amber),
        t.led_unlit_scale,
    };
}

static QColor mix(const QColor& a, const QColor& b, double t) {
    return QColor::fromRgbF(static_cast<float>(a.redF() + (b.redF() - a.redF()) * t),
                            static_cast<float>(a.greenF() + (b.greenF() - a.greenF()) * t),
                            static_cast<float>(a.blueF() + (b.blueF() - a.blueF()) * t));
}

// Every chrome tone from the OS palette (Mode::SYSTEM): the window, base, button, text,
// shadow and highlight colours as they are, the in-between tones mixed between them.
// Mixing toward the text colour darkens on a light scheme and lightens on a dark one,
// so one recipe serves both. Ours stay: the effect accents, the LED colours and the
// status bar's warning red.
static Colors from_system(const QPalette& sys, bool dark) {
    const QColor w = sys.color(QPalette::Window);
    const QColor base = sys.color(QPalette::Base);
    const QColor button = sys.color(QPalette::Button);
    const QColor text = sys.color(QPalette::WindowText);
    const QColor white(255, 255, 255);
    const QColor black(0, 0, 0);
    const PaletteHex& ours = dark ? DARK : LIGHT;
    const QColor border = mix(w, text, 0.22);
    return Colors{
        w, // window
        mix(w, base, 0.5), // surface
        button, // card
        mix(button, text, 0.04), // raised
        mix(button, text, 0.08), // control
        mix(button, text, 0.16), // control_hover
        mix(w, text, 0.40), // handle_hover
        mix(w, sys.color(QPalette::Highlight), 0.22), // selection
        mix(button, text, 0.22), // pressed
        mix(w, text, 0.12), // line
        border,
        mix(w, text, 0.36), // border_hover
        mix(w, button, 0.5), // disabled_bg
        mix(w, text, 0.08), // disabled_border
        sys.color(QPalette::Disabled, QPalette::WindowText),
        text,
        mix(text, w, 0.22), // text_dim
        mix(text, w, 0.32), // text_muted
        mix(text, w, 0.45), // text_faint
        mix(text, w, 0.58), // text_off
        QColor(ours.notice),
        sys.color(QPalette::Shadow),
        mix(button, text, 0.14), // track
        mix(button, text, 0.24), // track_hover
        dark ? mix(button, text, 0.06) : mix(button, white, 0.7), // knob_body_top
        dark ? mix(button, black, 0.25) : mix(button, text, 0.05), // knob_body_bot
        dark ? mix(w, black, 0.55) : mix(w, text, 0.30), // bezel
        mix(text, w, 0.58), // detent
        mix(base, text, 0.03), // well
        white, // thumb_top
        dark ? mix(white, w, 0.12) : mix(white, text, 0.05), // thumb_bot
        dark ? QColor() : border, // thumb_outline
        ours.switch_rim_off,
        ours.switch_rim_on,
        dark ? mix(w, black, 0.65) : mix(w, text, 0.06), // display_bg
        dark ? mix(w, text, 0.10) : border, // display_line
        dark ? mix(w, text, 0.10) : mix(w, text, 0.45), // display_faint
        QColor(ours.led_green),
        QColor(ours.led_red),
        QColor(ours.led_amber),
        ours.led_unlit_scale,
    };
}

static Mode current_mode = Mode::SYSTEM;
static bool dark_active = true;
static QColor current_accent("#3daee9"); // Breeze blue until the system says otherwise
static QPointer<QWidget> themed_root;

static Colors active = from_hex(DARK);

static const Colors& palette() {
    return active;
}

QColor generic_accent() {
    return current_accent;
}

QColor track() {
    return QColor(palette().track);
}
QColor track_hover() {
    return QColor(palette().track_hover);
}
QColor knob_body_top() {
    return QColor(palette().knob_body_top);
}
QColor knob_body_bot() {
    return QColor(palette().knob_body_bot);
}
QColor bezel() {
    return QColor(palette().bezel);
}
QColor detent() {
    return QColor(palette().detent);
}
QColor well() {
    return QColor(palette().well);
}
QColor dim_text() {
    return QColor(palette().text_dim);
}
QColor thumb_top() {
    return QColor(palette().thumb_top);
}
QColor thumb_bot() {
    return QColor(palette().thumb_bot);
}
QColor thumb_outline() {
    return QColor(palette().thumb_outline); // invalid when "" → no outline
}
int switch_rim_alpha(bool checked) {
    return checked ? palette().switch_rim_on : palette().switch_rim_off;
}
QColor display_line() {
    return QColor(palette().display_line);
}
QColor display_faint(int factor) {
    const QColor base(palette().display_faint);
    return dark_active ? base.lighter(factor) : base.darker(factor);
}
int unlit_alpha(int base_alpha) {
    return std::min(255, base_alpha * palette().led_unlit_scale);
}
QColor led_amber() {
    return QColor(palette().led_amber);
}

QColor meter_low() {
    return QColor("#5cc8a8");
}
QColor meter_mid() {
    return QColor("#f0c850");
}
QColor meter_high() {
    return QColor("#e85a4f");
}
QColor meter_clip() {
    return QColor("#ff5252");
}

QColor led_green() {
    return QColor(palette().led_green);
}
QColor led_red() {
    return QColor(palette().led_red);
}
QColor display_bg() {
    return QColor(palette().display_bg);
}
QColor flag_blue() {
    return QColor("#0039a6");
}
QColor flag_red() {
    return QColor("#d52b1e");
}
QColor flag_white() {
    return QColor("#ffffff");
}

static QColor readable_accent(const QColor& accent);

void tag_card(QWidget* card, const QString& fx_id) {
    if (!card) {
        return;
    }
    card->setProperty("fx", fx_id);

    // The app stylesheet disables parent→child palette propagation, so the
    // accents are pushed to the interactive descendants explicitly. Call
    // tagCard AFTER the card's knobs/toggles have been created.
    const QColor a = readable_accent(accent(fx_id));
    for (auto* k : card->findChildren<KnobWidget*>()) {
        k->set_accent(a);
    }
    for (auto* t : card->findChildren<ToggleSwitch*>()) {
        t->set_accent(a);
    }

    if (card->style()) {
        card->style()->unpolish(card);
        card->style()->polish(card);
    }
}

static const char* const STYLE_SHEET = R"(
/* === Window / base === */
QMainWindow, QDialog {
    background-color: @window@;
}
QWidget {
    color: @text@;
    font-size: 9pt;
}

/* === Menu bar === */
QLabel#madeInChile {
    color: @text@;
    font-weight: 600;
}
QMenuBar {
    background-color: @surface@;
    color: @text@;
    border-bottom: 1px solid @line@;
    padding: 2px 4px;
}
QMenuBar::item {
    background: transparent;
    padding: 4px 10px;
    border-radius: 4px;
}
QMenuBar::item:selected {
    background-color: @selection@;
}
QMenu {
    background-color: @surface@;
    color: @text@;
    border: 1px solid @line@;
    padding: 4px 0;
}
QMenu::item {
    padding: 6px 24px;
}
QMenu::item:selected {
    background-color: @selection@;
}
QMenu::separator {
    height: 1px;
    background: @line@;
    margin: 4px 6px;
}

/* === Buttons === */
QPushButton {
    background-color: @control@;
    color: @text@;
    border: 1px solid @border@;
    border-radius: 5px;
    padding: 6px 14px;
    min-height: 20px;
    font-weight: 500;
}
QPushButton:hover {
    background-color: @control_hover@;
    border-color: @border_hover@;
}
QPushButton:pressed {
    background-color: @pressed@;
}
QPushButton:disabled {
    background-color: @disabled_bg@;
    color: @disabled_text@;
    border-color: @disabled_border@;
}

/* === Effect row cards === */
QFrame[fxCard="true"] {
    background-color: @card@;
    border: 1px solid @line@;
    border-radius: 6px;
}
QLabel#fxGrip {
    color: @text_faint@;
    font-size: 12pt;
}
QLabel#fxName {
    font-weight: 700;
    font-size: 10pt;
}
/* Per-effect accent stripes and name colors are appended in apply(). */

/* === Scroll area === */
QScrollArea {
    background: transparent;
    border: none;
}
QScrollArea > QWidget > QWidget {
    background: transparent;
}
QScrollBar:horizontal {
    background: @window@;
    height: 10px;
    margin: 0;
    border: none;
}
QScrollBar::handle:horizontal {
    background: @control_hover@;
    border-radius: 4px;
    min-width: 24px;
    margin: 1px;
}
QScrollBar::handle:horizontal:hover {
    background: @handle_hover@;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0;
    background: none;
    border: none;
}
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
    background: none;
}
QScrollBar:vertical {
    background: @window@;
    width: 10px;
    margin: 0;
    border: none;
}
QScrollBar::handle:vertical {
    background: @control_hover@;
    border-radius: 4px;
    min-height: 24px;
    margin: 1px;
}
QScrollBar::handle:vertical:hover {
    background: @handle_hover@;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
    background: none;
    border: none;
}
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    background: none;
}

/* === Labels === */
QLabel {
    background: transparent;
}
QLabel#toggleCaption {
    color: @text_dim@;
}

/* === Status bar (bottom, VS Code style) === */
QStatusBar {
    background-color: @surface@;
    border-top: 1px solid @line@;
    min-height: 24px;
}
QStatusBar::item {
    border: none;
}
QLabel#statusItem, QLabel#statusAction {
    color: @text_muted@;
    padding: 2px 8px;
    border-radius: 4px;
}
QLabel#statusNotice {
    color: @notice@;
    font-weight: 600;
    padding: 2px 8px;
}
QLabel#statusAction:hover {
    background-color: @selection@;
    color: @text@;
}

/* === NAM/IR status labels (file row) === */
QLabel#fileStatus {
    color: @text_muted@;
    font-style: italic;
    padding-left: 6px;
}
QLabel#fileStatusOk {
    color: @accent@;
    font-weight: 600;
    padding-left: 6px;
}

/* === Hero rows (Amplifier / Cabinet): metadata + ‹ › arrows === */
QLabel#fileMeta {
    color: @text_dim@;
    padding-left: 8px;
}
QToolButton#heroArrow {
    color: @text@;
    font-size: 15pt;
    font-weight: 600;
    min-width: 22px;
    max-width: 22px;
    padding: 0 0 3px 0;
    border: 1px solid @border@;
    border-radius: 4px;
    background-color: @raised@;
}
QToolButton#heroArrow:hover {
    background-color: @selection@;
    border-color: @border_hover@;
}
QToolButton#heroArrow:pressed {
    background-color: @pressed@;
}

/* === Preset bar (top of the rack) === */
QFrame#presetBar {
    background-color: @surface@;
    border: 1px solid @line@;
    border-radius: 6px;
}
QLabel#presetCaption {
    color: @text_faint@;
    font-weight: 600;
    padding: 0 4px;
}
QToolButton#presetName {
    color: @accent@;
    font-size: 11pt;
    font-weight: 700;
    padding: 3px 10px;
    text-align: left;
    border: 1px solid @line@;
    border-radius: 5px;
    background-color: @window@;
}
QToolButton#presetName[untitled="true"] {
    color: @text_muted@;
    font-style: italic;
    font-weight: 500;
}
QToolButton#presetName:hover {
    border-color: @border_hover@;
}
QToolButton#presetName::menu-indicator {
    image: url(:/icons/down-arrow.png);
    subcontrol-position: right center;
    right: 8px;
}
/* Slots and A/B: filled buttons like Save; an empty slot is dimmer. */
QToolButton#presetSlot, QToolButton#presetAb {
    color: @text@;
    background-color: @raised@;
    border: 1px solid @border@;
    border-radius: 4px;
    padding: 3px 8px;
}
QToolButton#presetSlot:hover, QToolButton#presetAb:hover {
    background-color: @selection@;
    border-color: @border_hover@;
}
/* Push buttons (Save, Tap, A→B…) and menu buttons (4/4, Click…): filled and
   bordered so they read as clickable; menu buttons carry a ▾. */
QToolButton#presetTool, QToolButton#menuTool {
    color: @text@;
    background-color: @raised@;
    border: 1px solid @border@;
    border-radius: 4px;
    padding: 3px 9px;
}
QToolButton#presetTool:hover, QToolButton#menuTool:hover {
    background-color: @selection@;
    border-color: @border_hover@;
}
QToolButton#presetTool:pressed, QToolButton#menuTool:pressed {
    background-color: @pressed@;
}
QToolButton#menuTool {
    padding-right: 20px;
}
QToolButton#menuTool::menu-indicator {
    image: url(:/icons/down-arrow.png);
    subcontrol-origin: padding;
    subcontrol-position: right center;
    right: 7px;
}
QToolButton#presetSlot {
    color: @text_off@;
    background-color: @disabled_bg@;
    border-color: @line@;
    min-width: 14px;
    padding: 3px 5px;
    font-weight: 600;
}
QToolButton#presetSlot[filled="true"] {
    color: @text@;
    background-color: @raised@;
    border-color: @border@;
}
QToolButton#presetSlot[active="true"] {
    color: @on_accent@;
    background-color: @accent@;
    border-color: @accent@;
}
QToolButton#presetAb {
    min-width: 14px;
    font-weight: 700;
}
QToolButton#presetAb:checked {
    color: @on_accent@;
    background-color: @accent@;
    border-color: @accent@;
}

/* === Tempo bar (under the preset bar) === */
QFrame#tempoBar {
    background-color: @surface@;
    border: 1px solid @line@;
    border-radius: 6px;
}
QLineEdit#numberEdit {
    background-color: @window@;
    color: @accent@;
    border: 1px solid @border_hover@;
    border-radius: 4px;
    font-size: 12pt;
    font-weight: 700;
    selection-background-color: @accent@;
    selection-color: @on_accent@;
}

QToolButton#metroPlay {
    color: @text@;
    background-color: @control@;
    border: 1px solid @border@;
    border-radius: 5px;
    min-width: 26px;
    padding: 3px 6px;
    font-size: 11pt;
}
QToolButton#metroPlay:hover {
    background-color: @control_hover@;
}
QToolButton#metroPlay:checked {
    color: @on_accent@;
    background-color: @accent@;
    border-color: @accent@;
}

/* === Effect card ▾ detail toggle === */
QToolButton#cardMore {
    color: @text_muted@;
    background: transparent;
    border: none;
    border-radius: 4px;
    min-width: 22px;
    font-size: 14pt;
}
QToolButton#cardMore:hover {
    color: @text@;
    background-color: @selection@;
}

/* === MIDI Mappings window === */
QDialog#midiMappings QGroupBox {
    border: 1px solid @line@;
    border-radius: 6px;
    margin-top: 14px;
    padding: 8px;
}
QDialog#midiMappings QGroupBox::title {
    subcontrol-origin: margin;
    left: 8px;
    color: @text_muted@;
}
QDialog#midiMappings QTableWidget {
    background-color: @surface@;
    border: 1px solid @line@;
    border-radius: 5px;
    gridline-color: @line@;
}
QDialog#midiMappings QHeaderView::section {
    background-color: @card@;
    color: @text_muted@;
    border: none;
    border-bottom: 1px solid @line@;
    padding: 4px 6px;
    font-weight: 600;
}
QDialog#midiMappings QSpinBox {
    background-color: @window@;
    color: @text@;
    border: 1px solid @line@;
    border-radius: 4px;
    padding: 2px 4px;
}
QDialog#midiMappings QToolButton {
    color: @text@;
    background: transparent;
    border: 1px solid @border@;
    border-radius: 4px;
    padding: 2px 8px;
}
QDialog#midiMappings QToolButton:hover {
    background-color: @selection@;
}
QDialog#midiMappings QCheckBox {
    color: @text@;
}

/* === Library dock === */
QWidget#libraryPanel {
    background-color: @window@;
}
QToolButton#librarySection {
    color: @text_muted@;
    background: transparent;
    border: none;
    border-bottom: 2px solid transparent;
    padding: 5px 4px;
    font-weight: 600;
}
QToolButton#librarySection:hover {
    color: @text@;
}
QToolButton#librarySection:checked {
    color: @text@;
    border-bottom: 2px solid @accent@;
}
/* All / ★ / Recent, Folders…, rescan: filled like the preset bar's buttons;
   the active mode in the accent like A/B. */
QToolButton#libraryMode, QToolButton#libraryTool {
    color: @text@;
    background-color: @raised@;
    border: 1px solid @border@;
    border-radius: 4px;
    padding: 3px 9px;
}
QToolButton#libraryMode:hover, QToolButton#libraryTool:hover {
    background-color: @selection@;
    border-color: @border_hover@;
}
QToolButton#libraryMode:checked {
    color: @on_accent@;
    background-color: @accent@;
    border-color: @accent@;
}
QLineEdit#librarySearch {
    background-color: @surface@;
    color: @text@;
    border: 1px solid @line@;
    border-radius: 5px;
    padding: 5px 8px;
    selection-background-color: @accent@;
    selection-color: @on_accent@;
}
QLineEdit#librarySearch:focus {
    border-color: @accent@;
}
QListView#libraryList {
    background-color: @surface@;
    border: 1px solid @line@;
    border-radius: 5px;
    outline: none;
}
QLabel#libraryStatus {
    color: @text_muted@;
    padding-left: 4px;
}

/* === Dialogs inside the window tree (Library Folders) === */
QListWidget {
    background-color: @surface@;
    color: @text@;
    border: 1px solid @line@;
    border-radius: 5px;
    outline: none;
}
QListWidget::item {
    padding: 4px 6px;
}
QListWidget::item:selected {
    background-color: @selection@;
    color: @text@;
}
QTabWidget::pane {
    border: 1px solid @line@;
    border-radius: 5px;
    top: -1px;
}
QTabBar::tab {
    background: transparent;
    color: @text_muted@;
    padding: 6px 14px;
    border-bottom: 2px solid transparent;
}
QTabBar::tab:selected {
    color: @text@;
    border-bottom: 2px solid @accent@;
}
QTabBar::tab:hover {
    color: @text@;
}

/* === Combo boxes (library sort) === */
QComboBox {
    background-color: @control@;
    color: @text@;
    border: 1px solid @border@;
    border-radius: 5px;
    padding: 4px 8px;
    min-height: 20px;
    font-weight: 500;
}
QComboBox:hover {
    border-color: @border_hover@;
}
QComboBox:disabled {
    background-color: @disabled_bg@;
    color: @disabled_text@;
    border-color: @disabled_border@;
}
QComboBox::drop-down {
    border: none;
    width: 20px;
}
QComboBox::down-arrow {
    image: url(:/icons/down-arrow.png); /* @2x picked up on HiDPI */
    margin-right: 6px;
}
QComboBox::down-arrow:disabled {
    image: none;
}
QComboBox QAbstractItemView {
    background-color: @surface@;
    color: @text@;
    border: 1px solid @border@;
    selection-background-color: @selection@;
    selection-color: @text@;
    outline: none;
}
QComboBox QAbstractItemView::item {
    padding: 4px 8px;
    min-height: 24px;
}
QComboBox QAbstractItemView::item:hover {
    background-color: @selection@;
}

)";

// The only application-wide styling: tooltips are parentless top-levels, so
// they can't inherit the window stylesheet. Everything else lives in
// applyTo() — file dialogs are in-process Qt widgets even when "native" on
// KDE, and an app-wide stylesheet/palette would drag them into the dark
// theme; scoping to the window keeps them looking like the OS.
static const char* const APP_STYLE_SHEET = R"(
QToolTip {
    background-color: @raised@;
    color: @text@;
    border: 1px solid @border@;
    padding: 4px 6px;
    border-radius: 4px;
}
)";

// WCAG relative luminance and contrast ratio.
static double luminance(const QColor& c) {
    auto channel = [](double v) {
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
}
static double contrast(const QColor& a, const QColor& b) {
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// Text drawn on the accent (checked buttons, selections): the theme's window or text
// tone, whichever reads better on it.
static QColor on_accent() {
    const QColor& a = active.window;
    const QColor& b = active.text;
    return contrast(current_accent, a) >= contrast(current_accent, b) ? a : b;
}

// An effect accent on a light card (name, knob ring and tick, switch): the pale ones
// (Wah, Chorus…) are darkened until they read (3:1, WCAG's floor for graphics and
// large text). Dark theme: the accent verbatim, as approved.
static QColor readable_accent(const QColor& accent) {
    if (dark_active) {
        return accent;
    }
    QColor c = accent;
    const QColor& card = active.card;
    for (int i = 0; i < 12 && contrast(c, card) < 3.0; ++i) {
        c = c.darker(115);
    }
    return c;
}

static QString fill_roles(const char* sheet) {
    const Colors& pal = palette();
    const std::pair<const char*, QColor> roles[] = {
        {"window", pal.window},
        {"surface", pal.surface},
        {"card", pal.card},
        {"raised", pal.raised},
        {"control", pal.control},
        {"control_hover", pal.control_hover},
        {"handle_hover", pal.handle_hover},
        {"selection", pal.selection},
        {"pressed", pal.pressed},
        {"line", pal.line},
        {"border", pal.border},
        {"border_hover", pal.border_hover},
        {"disabled_bg", pal.disabled_bg},
        {"disabled_border", pal.disabled_border},
        {"disabled_text", pal.disabled_text},
        {"text", pal.text},
        {"text_dim", pal.text_dim},
        {"text_muted", pal.text_muted},
        {"text_faint", pal.text_faint},
        {"text_off", pal.text_off},
        {"notice", pal.notice},
        {"accent", current_accent},
        {"on_accent", on_accent()},
    };
    QString qss = QString::fromUtf8(sheet);
    for (const auto& [role, color] : roles) {
        qss.replace(QString("@%1@").arg(QLatin1String(role)), color.name());
    }
    return qss;
}

// Reads the scheme and accent the OS asks for (Mode::SYSTEM), or the forced scheme.
static void resolve_scheme() {
    const QPalette system = QGuiApplication::palette();
    current_accent =
        current_mode == Mode::SYSTEM ? system.color(QPalette::Highlight) : QColor("#3daee9");
    switch (current_mode) {
        case Mode::DARK:
            dark_active = true;
            break;
        case Mode::LIGHT:
            dark_active = false;
            break;
        case Mode::SYSTEM:
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
            if (QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Unknown) {
                dark_active = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
                break;
            }
#endif
            dark_active = system.color(QPalette::Window).lightness() < 128;
            break;
    }
    active = current_mode == Mode::SYSTEM ? from_system(system, dark_active)
                                          : from_hex(dark_active ? DARK : LIGHT);
}

// Follows the OS while Mode::SYSTEM: a scheme switch (Qt 6.5+) or a new system
// palette (an accent change; the only signal before 6.5).
class SystemThemeWatcher : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange &&
            current_mode == Mode::SYSTEM) {
            QTimer::singleShot(0, qApp, [] {
                reapply();
            });
        }
        return QObject::eventFilter(watched, event);
    }
};

void apply(QApplication& app) {
    current_mode = static_cast<Mode>(
        std::clamp(QSettings().value("ui/theme", 0).toInt(), 0, static_cast<int>(Mode::LIGHT)));
    resolve_scheme();
    app.setStyle("Fusion");
    app.setStyleSheet(fill_roles(APP_STYLE_SHEET));
    app.installEventFilter(new SystemThemeWatcher(&app));
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app, [] {
        if (current_mode == Mode::SYSTEM) {
            reapply();
        }
    });
#endif
}

void apply_to(QWidget& root) {
    themed_root = &root;
    const Colors& pal = palette();
    QPalette p;
    p.setColor(QPalette::Window, QColor(pal.window));
    p.setColor(QPalette::WindowText, QColor(pal.text));
    p.setColor(QPalette::Base, QColor(pal.surface));
    p.setColor(QPalette::AlternateBase, QColor(pal.card));
    p.setColor(QPalette::ToolTipBase, QColor(pal.raised));
    p.setColor(QPalette::ToolTipText, QColor(pal.text));
    p.setColor(QPalette::Text, QColor(pal.text));
    p.setColor(QPalette::Button, QColor(pal.control));
    p.setColor(QPalette::ButtonText, QColor(pal.text));
    p.setColor(QPalette::BrightText, QColor("#ffffff"));
    p.setColor(QPalette::Mid, QColor(pal.border));
    p.setColor(QPalette::Dark, QColor(pal.window));
    p.setColor(QPalette::Shadow, QColor(pal.shadow));
    p.setColor(QPalette::Highlight, current_accent);
    p.setColor(QPalette::HighlightedText, on_accent());

    p.setColor(QPalette::Disabled, QPalette::Text, QColor(pal.disabled_text));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(pal.disabled_text));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(pal.disabled_text));

    root.setPalette(p);

    // Per-effect accent rules — the palette hex verbatim (dark) for the name label
    // and the loaded-file label on the Amplifier/Cabinet rows (knob rings and
    // toggles get theirs pushed by tagCard).
    QString qss = fill_roles(STYLE_SHEET);
    for (const auto& fc : FX_COLORS) {
        qss += QString::fromUtf8("QFrame[fx=\"%1\"] QLabel#fxName { color: %2; }\n"
                                 "QFrame[fx=\"%1\"] QLabel#fileStatusOk { color: %2; }\n")
                   .arg(QLatin1String(fc.id), readable_accent(QColor(fc.accent)).name());
    }
    root.setStyleSheet(qss);
}

void reapply(bool force) {
    const QColor old_accent = current_accent;
    const QString old_sheet = fill_roles(STYLE_SHEET);
    resolve_scheme();
    if (!force && old_sheet == fill_roles(STYLE_SHEET)) {
        return; // the event was ours (or changed nothing we use): no loop, no flicker
    }
    qApp->setStyleSheet(fill_roles(APP_STYLE_SHEET));
    if (!themed_root) {
        return;
    }
    apply_to(*themed_root);
    // Widgets holding the app accent (untagged knobs and switches) take the new one;
    // effect accents stay. Custom-painted widgets read the chrome tones when they paint.
    if (old_accent != current_accent) {
        for (auto* k : themed_root->findChildren<KnobWidget*>()) {
            if (k->get_accent() == old_accent) {
                k->set_accent(current_accent);
            }
        }
        for (auto* t : themed_root->findChildren<ToggleSwitch*>()) {
            if (t->get_accent() == old_accent) {
                t->set_accent(current_accent);
            }
        }
    }
    // Effect cards: their accents may change with the theme (readable_accent).
    for (auto* w : themed_root->findChildren<QWidget*>()) {
        const QVariant fx = w->property("fx");
        if (fx.isValid()) {
            tag_card(w, fx.toString());
        }
    }
    for (auto* w : themed_root->findChildren<QWidget*>()) {
        w->update();
    }
}

Mode get_mode() {
    return current_mode;
}

void set_mode(Mode new_mode) {
    current_mode = new_mode;
    QSettings().setValue("ui/theme", static_cast<int>(new_mode));
    reapply(true);
}

} // namespace Theme

} // namespace namx
