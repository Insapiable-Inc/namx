#include "Theme.h"

#include <algorithm>
#include <cassert>
#include <cmath>

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHash>
#include <QImage>
#include <QPalette>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleHints>
#include <QTextDocument> // Qt::mightBeRichText
#include <QTimer>
#include <QVariant>
#include <QWidget>

#include "Common.h"
#include "KnobWidget.h"
#include "ThemeFile.h"
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
    {"merge", "#4f9fe8"},   {"neutral", "#7d8590"},
};

// A theme file's effect accents (Mode::CUSTOM; empty otherwise).
static QHash<QString, QColor> effect_overrides;

QColor accent(const QString& fx_id) {
    if (const auto it = effect_overrides.constFind(fx_id); it != effect_overrides.cend()) {
        return *it;
    }
    for (const auto& fc : FX_COLORS) {
        if (fx_id == QLatin1String(fc.id)) {
            return QColor(fc.accent);
        }
    }
    return QColor("#7d8590");
}

QStringList effect_ids() {
    QStringList ids;
    for (const auto& fc : FX_COLORS) {
        if (fc.id != string_view("neutral")) {
            ids.append(QLatin1String(fc.id));
        }
    }
    return ids;
}

// What stays ours in every theme: the warning red, the switch edge, the LED colours.
// The displays' LED colours are deeper on a light panel so they still read.
struct OwnTones
{
    const char* notice; // status bar problems
    int switch_rim_off; // alpha of the switch track's dark edge, off / on
    int switch_rim_on;
    const char* led_green;
    const char* led_red;
    const char* led_amber; // the meters' hot zone
    int led_unlit_scale; // unlit segments' tint strength (alpha multiplier)
};

static constexpr OwnTones OWN_DARK = {"#ff5252", 100, 60, "#42e86c", "#ff453a", "#f0c850", 1};
static constexpr OwnTones OWN_LIGHT = {"#c62828", 40, 30, "#16a34a", "#dc2626", "#d19a00", 2};

// The tones in use, derived from a palette (from_palette).
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

// The forced Dark / Light themes: KDE's Breeze Dark and Breeze Light colour schemes
// (/usr/share/color-schemes/Breeze*.colors), as Qt's KDE platform theme turns them into
// a QPalette — disabled text and shadow included, read from a dump under each scheme
// (2026-10-09). So "Dark" looks like following the system under Breeze Dark, anywhere.
static QPalette breeze(bool dark) {
    struct Roles
    {
        const char* window;
        const char* window_text;
        const char* base;
        const char* button;
        const char* highlight;
        const char* shadow;
        const char* disabled_text;
    };
    static constexpr Roles DARK = {"#202326", "#fcfcfc", "#141618", "#292c30",
                                   "#3daee9", "#0e0f10", "#686a6c"};
    static constexpr Roles LIGHT = {"#eff0f1", "#232629", "#ffffff", "#fcfcfc",
                                    "#3daee9", "#474a4c", "#a0a1a3"};
    const Roles& r = dark ? DARK : LIGHT;
    QPalette p;
    p.setColor(QPalette::Window, QColor(r.window));
    p.setColor(QPalette::WindowText, QColor(r.window_text));
    p.setColor(QPalette::Base, QColor(r.base));
    p.setColor(QPalette::Button, QColor(r.button));
    p.setColor(QPalette::Highlight, QColor(r.highlight));
    p.setColor(QPalette::Shadow, QColor(r.shadow));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(r.disabled_text));
    return p;
}

// Every chrome tone from a palette (the OS's, or Breeze's when forced): the window, base,
// button, text, shadow and highlight colours as they are, the in-between tones mixed
// between them.
// Mixing toward the text colour darkens on a light scheme and lightens on a dark one,
// so one recipe serves both. Ours stay: the effect accents, the LED colours and the
// status bar's warning red.
static Colors from_palette(const QPalette& sys, bool dark) {
    const QColor w = sys.color(QPalette::Window);
    const QColor base = sys.color(QPalette::Base);
    const QColor button = sys.color(QPalette::Button);
    const QColor text = sys.color(QPalette::WindowText);
    const QColor white(255, 255, 255);
    const QColor black(0, 0, 0);
    const OwnTones& ours = dark ? OWN_DARK : OWN_LIGHT;
    const QColor border = mix_colors(w, text, 0.22);
    return Colors{
        w, // window
        mix_colors(w, base, 0.5), // surface
        button, // card
        mix_colors(button, text, 0.04), // raised
        mix_colors(button, text, 0.08), // control
        mix_colors(button, text, 0.16), // control_hover
        mix_colors(w, text, 0.40), // handle_hover
        mix_colors(w, sys.color(QPalette::Highlight), 0.22), // selection
        mix_colors(button, text, 0.22), // pressed
        mix_colors(w, text, 0.12), // line
        border,
        mix_colors(w, text, 0.36), // border_hover
        mix_colors(w, button, 0.5), // disabled_bg
        mix_colors(w, text, 0.08), // disabled_border
        sys.color(QPalette::Disabled, QPalette::WindowText),
        text,
        mix_colors(text, w, 0.22), // text_dim
        mix_colors(text, w, 0.32), // text_muted
        mix_colors(text, w, 0.45), // text_faint
        mix_colors(text, w, 0.58), // text_off
        QColor(ours.notice),
        sys.color(QPalette::Shadow),
        mix_colors(button, text, 0.14), // track
        mix_colors(button, text, 0.24), // track_hover
        dark ? mix_colors(button, text, 0.06) : mix_colors(button, white, 0.7), // knob_body_top
        dark ? mix_colors(button, black, 0.25) : mix_colors(button, text, 0.05), // knob_body_bot
        dark ? mix_colors(w, black, 0.55) : mix_colors(w, text, 0.30), // bezel
        mix_colors(text, w, 0.58), // detent
        mix_colors(base, text, 0.03), // well
        white, // thumb_top
        dark ? mix_colors(white, w, 0.12) : mix_colors(white, text, 0.05), // thumb_bot
        dark ? QColor() : border, // thumb_outline
        ours.switch_rim_off,
        ours.switch_rim_on,
        dark ? mix_colors(w, black, 0.65) : mix_colors(w, text, 0.06), // display_bg
        dark ? mix_colors(w, text, 0.10) : border, // display_line
        dark ? mix_colors(w, text, 0.10) : mix_colors(w, text, 0.45), // display_faint
        QColor(ours.led_green),
        QColor(ours.led_red),
        QColor(ours.led_amber),
        ours.led_unlit_scale,
    };
}

static Mode current_mode = Mode::SYSTEM;
static ThemeSpec custom_theme; // Mode::CUSTOM
static QString custom_theme_path;
static bool dark_active = true;
static QColor current_accent("#3daee9"); // Breeze blue until the system says otherwise
static QPointer<QWidget> themed_root;

static Colors active = from_palette(breeze(true), true);

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

QColor led_green() {
    return QColor(palette().led_green);
}
QColor led_red() {
    return QColor(palette().led_red);
}
QColor display_bg() {
    return QColor(palette().display_bg);
}
static QColor readable_accent(const QColor& accent);

QColor fx_accent(const QString& fx_id) {
    return fx_id == QLatin1String("neutral") ? current_accent : readable_accent(accent(fx_id));
}
QColor canvas_bg() {
    return mix_colors(palette().window, dark_active ? QColor(0, 0, 0) : QColor(255, 255, 255),
                      0.25);
}
QColor canvas_dot() {
    return mix_colors(canvas_bg(), palette().text, 0.12);
}
QColor node_fill() {
    return palette().card;
}
QColor node_text() {
    return palette().text;
}
QColor node_text_dim() {
    return palette().text_muted;
}
QColor wire_drag() {
    return palette().text_dim;
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

    // Knobs and switches paint their own accent (not a palette role), so it is pushed
    // to each one. Call tag_card AFTER the card's knobs/toggles have been created.
    // Non-effect cards (Input, Output: gain knobs, the limiter) take the app accent,
    // like the tempo bar's knobs.
    const QColor a =
        fx_id == QLatin1String("neutral") ? current_accent : readable_accent(accent(fx_id));
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
    image: url(@arrow@);
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
    image: url(@arrow@);
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
/* No spin box rule: a style-sheet box makes Fusion drop the arrows, and the palette
   already reaches them (AA_UseStyleSheetPropagationInWidgetStyles). */
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
    image: url(@arrow@); /* @2x picked up on HiDPI */
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


/* === Node-graph UI (ui/node-graph): top bar === */
QFrame#topBar {
    background-color: @surface@;
    border-bottom: 1px solid @line@;
}
QLabel#logo {
    font-size: 20pt;
    font-weight: 300;
    letter-spacing: 3px;
    color: @text@;
}
QFrame#topBarLine {
    background-color: @line@;
    margin: 6px 0;
}
QToolButton#presetTitle {
    font-size: 15pt;
    color: @text@;
    background: transparent;
    border: none;
    border-radius: 6px;
    padding: 4px 10px;
}
QToolButton#presetTitle[untitled="true"] {
    color: @text_muted@;
}
QToolButton#presetTitle:hover {
    background-color: @selection@;
}
QToolButton#presetTitle::menu-indicator {
    image: none;
    width: 0;
}
QToolButton#topBarTool, QToolButton#metroToggle {
    color: @text@;
    background-color: @raised@;
    border: 1px solid @border@;
    border-radius: 6px;
    min-width: 30px;
    min-height: 30px;
    font-size: 13pt;
}
QToolButton#topBarTool:hover, QToolButton#metroToggle:hover {
    background-color: @selection@;
    border-color: @border_hover@;
}
QToolButton#topBarTool::menu-indicator {
    image: none;
    width: 0;
}
QToolButton#topBarTool:checked {
    color: @accent@;
}
QFrame#metroBox {
    background-color: @raised@;
    border: 1px solid @border@;
    border-radius: 7px;
}
QFrame#metroBox QToolButton#topBarTool, QFrame#metroBox QToolButton#metroToggle {
    border: none;
    background: transparent;
    min-width: 28px;
    min-height: 28px;
}
QFrame#metroBox QToolButton#metroToggle:checked {
    color: @on_accent@;
    background-color: @accent@;
}
QLineEdit#topSearch {
    background-color: @raised@;
    color: @text@;
    border: 1px solid @border@;
    border-radius: 7px;
    padding: 7px 10px;
    selection-background-color: @accent@;
    selection-color: @on_accent@;
}
QLineEdit#topSearch:focus {
    border-color: @accent@;
}
QFrame#tempoPopup {
    background-color: @surface@;
    border: 1px solid @border@;
    border-radius: 6px;
}

QToolButton#tunerToggle {
    color: @text@;
    background-color: @raised@;
    border: 1px solid @border@;
    border-radius: 6px;
    min-height: 30px;
    padding: 0 12px;
    font-weight: 600;
}
QToolButton#tunerToggle:hover {
    background-color: @selection@;
    border-color: @border_hover@;
}
QToolButton#tunerToggle:checked {
    color: @on_accent@;
    background-color: @accent@;
    border-color: @accent@;
}
QFrame#tunerStrip {
    background-color: @surface@;
    border-bottom: 1px solid @line@;
}

/* === Node-graph UI: canvas tools === */
QWidget#canvasTools {
    background-color: @surface@;
    border: 1px solid @line@;
    border-radius: 7px;
}
QToolButton#canvasTool {
    color: @text@;
    background: transparent;
    border: none;
    border-radius: 5px;
    min-width: 30px;
    min-height: 30px;
    font-size: 14pt;
}
QToolButton#canvasTool:hover {
    background-color: @selection@;
}
QSplitter#rigSplit::handle {
    background-color: @line@;
    width: 1px;
}

/* === Node-graph UI: parameter panel === */
QLabel#knobName {
    color: @text_dim@;
    font-size: 9.5pt;
}
/* No `color`: KnobWidget gives the value its effect's accent. */
QLineEdit#knobField {
    background-color: @window@;
    border: 1px solid @border@;
    border-radius: 6px;
    padding: 0 6px;
    font-size: 11.5pt;
    font-weight: 600;
}
QLineEdit#knobField:hover {
    border-color: @border_hover@;
}
QWidget#paramPanel, QFrame#panelBody, QScrollArea#panelScroll, QWidget#panelContent {
    background-color: @window@;
}
QLabel#panelTitle {
    font-size: 17pt;
    font-weight: 600;
}
QLabel#panelSubtitle {
    color: @text_muted@;
    font-size: 10pt;
}
QLabel#panelHint {
    color: @text_muted@;
    font-size: 10pt;
}
QLabel#panelCaption {
    color: @text_muted@;
    font-size: 8.5pt;
    font-weight: 700;
    letter-spacing: 1px;
}
QFrame#panelLine {
    background-color: @line@;
}
QFrame#panelGroup {
    background-color: @card@;
    border: 1px solid @line@;
    border-radius: 8px;
}
QToolButton#powerButton {
    color: @text_muted@;
    background: transparent;
    border: 2px solid @border@;
    border-radius: 19px;
    min-width: 34px;
    min-height: 34px;
    font-size: 15pt;
}
QToolButton#powerButton:hover {
    border-color: @border_hover@;
}
QToolButton#powerButton:checked {
    color: @accent@;
    border-color: @accent@;
}
QToolButton#panelMore {
    color: @text@;
    background: transparent;
    border: none;
    border-radius: 5px;
    min-width: 26px;
    min-height: 30px;
    font-size: 15pt;
}
QToolButton#panelMore::menu-indicator {
    image: none;
    width: 0;
}
QToolButton#panelMore:hover {
    background-color: @selection@;
}
QToolButton#panelTool {
    color: @text@;
    background-color: @raised@;
    border: 1px solid @border@;
    border-radius: 5px;
    padding: 4px 10px;
}
QToolButton#panelTool:hover {
    background-color: @selection@;
    border-color: @border_hover@;
}
QToolButton#segment {
    color: @text@;
    background-color: @raised@;
    border: 1px solid @border@;
    border-radius: 0;
    padding: 5px 14px;
}
QToolButton#segment[first="true"] {
    border-top-left-radius: 5px;
    border-bottom-left-radius: 5px;
}
QToolButton#segment[last="true"] {
    border-top-right-radius: 5px;
    border-bottom-right-radius: 5px;
}
QToolButton#segment:hover {
    background-color: @selection@;
}
QToolButton#segment:checked {
    color: @on_accent@;
    background-color: @accent@;
    border-color: @accent@;
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
// large text). Dark theme: the accent verbatim, as approved — except under a theme
// file, whose cards can be any tone: there the dark ones are lightened the same way.
static QColor readable_on(const QColor& color, const QColor& background, double ratio);

static QColor readable_accent(const QColor& accent) {
    if (dark_active && current_mode != Mode::CUSTOM) {
        return accent;
    }
    return readable_on(accent, active.card, 3.0);
}

// `color` moved away from `background` (darker on a light one, lighter on a dark one)
// until the two reach `ratio`; unchanged when they already do.
static QColor readable_on(const QColor& color, const QColor& background, double ratio) {
    const bool light_background = luminance(background) > 0.18;
    QColor c = color;
    for (int i = 0; i < 16 && contrast(c, background) < ratio; ++i) {
        c = light_background ? c.darker(115) : c.lighter(115);
    }
    return c;
}

// The status colours as approved on dark, made readable (4.5:1, WCAG's floor for text —
// the DSP load is text) on the status bar's tone.
QColor status_ok() {
    return readable_on(QColor("#5cc8a8"), active.surface, 4.5);
}
QColor status_warn() {
    return readable_on(QColor("#f0c850"), active.surface, 4.5);
}
QColor status_bad() {
    return readable_on(QColor("#e85a4f"), active.surface, 4.5);
}

// The ▾ arrow on combo boxes and menu buttons: the resource image where its grey reads
// on the controls (3:1, as for graphics), else a copy tinted readable, written to the
// cache folder (style sheets take only files) with its @2x twin.
static QString arrow_url() {
    static const QColor ARROW_GREY(153, 160, 172); // the PNGs' colour
    const QColor tint = readable_on(ARROW_GREY, active.control, 3.0);
    if (tint == ARROW_GREY) {
        return ":/icons/down-arrow.png";
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString name = QString("down-arrow-%1").arg(tint.name().mid(1));
    const QString path = dir + '/' + name + ".png";
    if (QFileInfo::exists(path) && QFileInfo::exists(dir + '/' + name + "@2x.png")) {
        return path;
    }
    QDir().mkpath(dir);
    for (const char* suffix : {"", "@2x"}) {
        QImage image(QString(":/icons/down-arrow%1.png").arg(QLatin1String(suffix)));
        image = image.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                QColor c = tint;
                c.setAlpha(qAlpha(image.pixel(x, y)));
                image.setPixelColor(x, y, c);
            }
        }
        if (!image.save(dir + '/' + name + QLatin1String(suffix) + ".png")) {
            return ":/icons/down-arrow.png"; // no cache folder: the grey one beats none
        }
    }
    return path;
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
    if (qss.contains("@arrow@")) {
        qss.replace("@arrow@", arrow_url());
    }
    return qss;
}

// A theme file's base colours as the palette from_palette reads.
static QPalette palette_of(const ThemeSpec& spec) {
    QPalette p;
    p.setColor(QPalette::Window, spec.window);
    p.setColor(QPalette::WindowText, spec.text);
    p.setColor(QPalette::Base, spec.base);
    p.setColor(QPalette::Button, spec.button);
    p.setColor(QPalette::Highlight, spec.accent);
    p.setColor(QPalette::Shadow, spec.shadow);
    p.setColor(QPalette::Disabled, QPalette::WindowText, spec.disabled_text);
    return p;
}

// Reads the scheme and accent the OS asks for (Mode::SYSTEM), the forced Breeze one, or
// the theme file's.
static void resolve_scheme() {
    const QPalette system = QGuiApplication::palette();
    effect_overrides.clear();
    switch (current_mode) {
        case Mode::CUSTOM:
            dark_active = custom_theme.window.lightness() < 128;
            effect_overrides = custom_theme.effects;
            break;
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
    const QPalette source = current_mode == Mode::SYSTEM   ? system
                            : current_mode == Mode::CUSTOM ? palette_of(custom_theme)
                                                           : breeze(dark_active);
    current_accent = source.color(QPalette::Highlight);
    active = from_palette(source, dark_active);
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

// The theme file in use, watched so that saving it restyles the window (editors that
// save by replacing the file drop it from the watch; it is added back).
static QFileSystemWatcher* custom_watcher = nullptr;

static void watch_custom_file() {
    if (!custom_watcher) {
        custom_watcher = new QFileSystemWatcher(qApp);
        QObject::connect(custom_watcher, &QFileSystemWatcher::fileChanged, qApp, [] {
            // Let the editor finish writing (a replace is a remove plus a create).
            QTimer::singleShot(200, qApp, [] {
                if (current_mode != Mode::CUSTOM) {
                    return;
                }
                watch_custom_file();
                const auto spec = load_theme_file(custom_theme_path, effect_ids());
                if (!spec) {
                    qWarning("Theme file %s: %s", qPrintable(custom_theme_path),
                             qPrintable(spec.error()));
                    return; // keep the last good version
                }
                custom_theme = *spec;
                reapply(true);
            });
        });
    }
    if (!custom_watcher->files().isEmpty()) {
        custom_watcher->removePaths(custom_watcher->files());
    }
    if (current_mode == Mode::CUSTOM) {
        custom_watcher->addPath(custom_theme_path);
    }
}

void apply(QApplication& app) {
    current_mode = static_cast<Mode>(
        std::clamp(QSettings().value("ui/theme", 0).toInt(), 0, static_cast<int>(Mode::CUSTOM)));
    if (current_mode == Mode::CUSTOM) {
        custom_theme_path = QSettings().value("ui/customTheme").toString();
        const auto spec = load_theme_file(custom_theme_path, effect_ids());
        if (spec) {
            custom_theme = *spec;
            watch_custom_file();
        } else {
            // This run only: the setting stays, so the theme is back once the file is.
            qWarning("Theme file %s: %s; following the system instead",
                     qPrintable(custom_theme_path), qPrintable(spec.error()));
            current_mode = Mode::SYSTEM;
        }
    }
    resolve_scheme();
    // Under a style sheet a widget takes the application palette (the OS's), not its
    // parent's — so a window created later than apply_to (Settings, MIDI Mappings)
    // showed the OS's base colour in its check boxes, spin boxes and tables: black ones
    // on the Light theme under a dark desktop. This attribute restores the widget-style
    // propagation, so the window's palette reaches every descendant.
    QApplication::setAttribute(Qt::AA_UseStyleSheetPropagationInWidgetStyles, true);
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
                                 "QFrame[fx=\"%1\"] QLabel#fileStatusOk { color: %2; }\n"
                                 "QFrame[fx=\"%1\"] QToolButton#powerButton:checked { color: "
                                 "%2; border-color: %2; }\n"
                                 "QFrame[fx=\"%1\"] QLineEdit#knobField:focus { "
                                 "border-color: %2; }\n"
                                 "QFrame[fx=\"%1\"] QToolButton#segment:checked { "
                                 "background-color: %2; border-color: %2; color: %3; }\n")
                   .arg(QLatin1String(fc.id), readable_accent(accent(fc.id)).name(),
                        luminance(readable_accent(accent(fc.id))) > 0.35 ? "#000000" : "#ffffff");
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
    assert(new_mode != Mode::CUSTOM);
    current_mode = new_mode;
    QSettings().setValue("ui/theme", static_cast<int>(new_mode));
    watch_custom_file();
    reapply(true);
}

expected<void, QString> set_custom(const QString& path) {
    auto spec = load_theme_file(path, effect_ids());
    if (!spec) {
        return std::unexpected(spec.error());
    }
    custom_theme = std::move(*spec);
    custom_theme_path = path;
    current_mode = Mode::CUSTOM;
    QSettings settings;
    settings.setValue("ui/theme", static_cast<int>(Mode::CUSTOM));
    settings.setValue("ui/customTheme", path);
    watch_custom_file();
    reapply(true);
    return {};
}

QString custom_path() {
    return current_mode == Mode::CUSTOM ? custom_theme_path : QString();
}

} // namespace Theme

} // namespace namx
