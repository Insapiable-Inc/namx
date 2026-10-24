#include "Theme.h"

#include <algorithm>
#include <cassert>
#include <cmath>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPointer>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QStyleOptionButton>
#include <QStyledItemDelegate>
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
    {"neutral", "#7d8590"},
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
    QColor selection; // hovered / selected list items
    QColor pressed; // pressed buttons: tinted toward the accent
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
    // KDE's: the menu bar's Header background, frames (the background 20 % toward the
    // text, KDE's frame contrast), and the accent-tinted hover of menu items.
    QColor header;
    QColor frame; // menu borders, separators, the line over the status bar
    QColor header_hover; // menu bar items
    QColor menu_hover; // menu items
    QColor button_frame; // check boxes, table headers: the button 20 % toward the text
    QColor tab; // closed tabs: the view background 17 % toward the text (Union's)
    QColor checked; // checked buttons: the button 12.5 % toward the text (Union's)
    QColor bar; // scroll bar handles: the window 7.5 % toward the text (Union's)
    QColor bar_hover; // … hovered or dragged: a third toward the accent
    QColor view; // lists, fields: the palette's base, as is (Union's view background)
    QColor view_frame; // their frame: the view 20 % toward the text
    QColor list_hover; // hovered list rows: the accent at 30 % over the view
    QColor highlighted_text; // text on the accent: selected rows (the palette's)
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
        const char* highlighted_text;
        const char* shadow;
        const char* disabled_text;
    };
    static constexpr Roles DARK = {"#202326", "#fcfcfc", "#141618", "#292c30",
                                   "#3daee9", "#fcfcfc", "#0e0f10", "#686a6c"};
    static constexpr Roles LIGHT = {"#eff0f1", "#232629", "#ffffff", "#fcfcfc",
                                    "#3daee9", "#ffffff", "#474a4c", "#a0a1a3"};
    const Roles& r = dark ? DARK : LIGHT;
    QPalette p;
    p.setColor(QPalette::Window, QColor(r.window));
    p.setColor(QPalette::WindowText, QColor(r.window_text));
    p.setColor(QPalette::Base, QColor(r.base));
    p.setColor(QPalette::Button, QColor(r.button));
    p.setColor(QPalette::Highlight, QColor(r.highlight));
    p.setColor(QPalette::HighlightedText, QColor(r.highlighted_text));
    p.setColor(QPalette::Shadow, QColor(r.shadow));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(r.disabled_text));
    return p;
}

// Every chrome tone from a palette (the OS's, or Breeze's when forced) and the menu bar's
// header colour: the window, base, button, text, shadow and highlight colours as they
// are, the in-between tones mixed between them.
// Mixing toward the text colour darkens on a light scheme and lightens on a dark one,
// so one recipe serves both. Ours stay: the effect accents, the LED colours and the
// status bar's warning red.
static Colors from_palette(const QPalette& sys, bool dark, const QColor& header) {
    const QColor w = sys.color(QPalette::Window);
    const QColor accent = sys.color(QPalette::Highlight);
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
        mix_colors(w, accent, 0.22), // selection
        mix_colors(button, accent, 1.0 / 3), // pressed
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
        header,
        mix_colors(w, text, 0.2), // frame
        mix_colors(header, accent, 0.3), // header_hover
        mix_colors(w, accent, 0.3), // menu_hover
        mix_colors(button, text, 0.2), // button_frame
        mix_colors(base, text, 0.17), // tab
        mix_colors(button, text, 0.125), // checked
        mix_colors(w, text, 0.075), // bar
        mix_colors(w, accent, 1.0 / 3), // bar_hover
        base, // view
        mix_colors(base, text, 0.2), // view_frame
        mix_colors(base, accent, 0.3), // list_hover
        sys.color(QPalette::HighlightedText),
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
// The style the desktop asked for (Union, Breeze, Windows 11…, or -style / QT_STYLE_OVERRIDE),
// as Qt chose it before NAM-X changed anything; Fusion where nothing else is available.
static QString native_style;
// The style in use. Tracked here: with a stylesheet set, QApplication::style() is Qt's
// stylesheet wrapper, which has no name.
static QString active_style;

// Fusion carries the Union-like look itself (FUSION_SHEET, UnionPolish); a native style
// draws the standard widgets its own way.
static bool fusion_active() {
    return active_style.compare("fusion", Qt::CaseInsensitive) == 0;
}

// The desktop's style while following the system; Fusion under a forced theme, since
// native styles may ignore the palette (Union does: it reads KDE's colour scheme). True
// when it changed the style.
static bool choose_style() {
    const QString wanted = current_mode == Mode::SYSTEM ? native_style : QStringLiteral("Fusion");
    if (active_style.compare(wanted, Qt::CaseInsensitive) != 0) {
        // Named before it is installed: installing it re-polishes every widget, and the
        // polish filter has to know which style it is polishing for.
        if (QStyle* style = QStyleFactory::create(wanted)) {
            active_style = style->name();
            QApplication::setStyle(style);
            return true;
        }
    }
    return false;
}

// Breeze's Header background (the menu bar), Dark and Light.
static QColor breeze_header(bool dark) {
    return QColor(dark ? "#292c30" : "#dee0e2");
}

static Colors active = from_palette(breeze(true), true, breeze_header(true));

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
QColor list_hover() {
    return palette().list_hover;
}
QColor highlighted_text() {
    return palette().highlighted_text;
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
}
/* === Menu bar corner === */
QLabel#madeInChile {
    color: @text@;
    font-weight: 600;
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
    font-size: @pt:10@;
}
/* Per-effect accent stripes and name colors are appended in apply(). */
/* === Scroll area === */
QScrollArea {
    background: transparent;
    border: none;
}
/* The rack itself (through the viewport — not the scroll bars, which the style draws). */
QScrollArea > QWidget#qt_scrollarea_viewport > QWidget {
    background: transparent;
}
/* === Labels === */
QLabel {
    background: transparent;
}
QLabel#toggleCaption {
    color: @text_dim@;
}
QLabel#statusItem, QLabel#statusAction {
    color: @text_muted@;
    padding: 2px 8px;
    border: 1px solid transparent;
    border-radius: 5px;
}
QLabel#statusNotice {
    color: @notice@;
    font-weight: 600;
    padding: 2px 8px;
}
/* Clickable items hover like Union's flat buttons: the accent on the border. */
QLabel#statusAction:hover {
    border-color: @accent@;
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
/* Square, as wide as they are tall (MainWindow::fit_to_font sizes them). */
QToolButton#heroArrow {
    color: @text@;
    font-size: @pt:15@;
    font-weight: 600;
    padding: 0 0 3px 0;
    border: 1px solid @button_frame@;
    border-radius: 5px;
    background-color: @button@;
}
QToolButton#heroArrow:hover {
    border-color: @accent@;
}
QToolButton#heroArrow:pressed {
    background-color: @pressed@;
    border-color: @accent@;
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
    font-size: @pt:11@;
    font-weight: 700;
    padding: 3px 10px;
    text-align: left;
    border: 1px solid @button_frame@;
    border-radius: 5px;
    background-color: @button@;
}
QToolButton#presetName[untitled="true"] {
    color: @text_muted@;
    font-style: italic;
    font-weight: 500;
}
QToolButton#presetName:hover {
    border-color: @accent@;
}
QToolButton#presetName::menu-indicator {
    image: url(@arrow@);
    width: 16px;
    height: 16px;
    subcontrol-position: right center;
    right: 6px;
}
/* Slots and A/B: filled buttons like Save; an empty slot is dimmer. */
QToolButton#presetSlot, QToolButton#presetAb {
    color: @text@;
    background-color: @button@;
    border: 1px solid @button_frame@;
    border-radius: 5px;
    padding: 3px 8px;
}
QToolButton#presetSlot:hover, QToolButton#presetAb:hover {
    border-color: @accent@;
}
QToolButton#presetSlot:pressed, QToolButton#presetAb:pressed {
    background-color: @pressed@;
    border-color: @accent@;
}
/* Push buttons (Save, Tap, A→B…) and menu buttons (4/4, Click…): filled and
   bordered so they read as clickable; menu buttons carry a ▾. */
QToolButton#presetTool, QToolButton#menuTool {
    color: @text@;
    background-color: @button@;
    border: 1px solid @button_frame@;
    border-radius: 5px;
    padding: 3px 9px;
}
QToolButton#presetTool:hover, QToolButton#menuTool:hover {
    border-color: @accent@;
}
QToolButton#presetTool:pressed, QToolButton#menuTool:pressed {
    background-color: @pressed@;
    border-color: @accent@;
}
/* Union's: the text, 4 px, the 16 px chevron, 6 px to the border. */
QToolButton#menuTool {
    padding-right: 26px;
}
QToolButton#menuTool::menu-indicator {
    image: url(@arrow@);
    width: 16px;
    height: 16px;
    subcontrol-origin: padding;
    subcontrol-position: right center;
    right: 6px;
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
    background-color: @button@;
    border-color: @button_frame@;
}
/* Checked (and the active slot): Union's — the button 12.5 % toward the text, its border
   and text as they are, so hover still shows the accent border. */
QToolButton#presetSlot[active="true"] {
    color: @text@;
    background-color: @checked@;
}
QToolButton#presetAb {
    min-width: 14px;
    font-weight: 700;
}
QToolButton#presetAb:checked {
    background-color: @checked@;
}
/* === Tempo bar (under the preset bar) === */
QFrame#tempoBar {
    background-color: @surface@;
    border: 1px solid @line@;
    border-radius: 6px;
}
QToolButton#metroPlay {
    color: @text@;
    background-color: @button@;
    border: 1px solid @button_frame@;
    border-radius: 5px;
    min-width: 26px;
    padding: 3px 6px;
    font-size: 11pt;
}
QToolButton#metroPlay:hover {
    border-color: @accent@;
}
QToolButton#metroPlay:pressed {
    background-color: @pressed@;
    border-color: @accent@;
}
QToolButton#metroPlay:checked {
    background-color: @checked@;
}
/* === Effect card ▾ detail toggle === */
/* A flat button: KDE gives it just the accent border on hover. */
QToolButton#cardMore {
    color: @text_muted@;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 5px;
    min-width: 22px;
    font-size: 14pt;
}
QToolButton#cardMore:hover {
    color: @text@;
    border-color: @accent@;
}
QToolButton#cardMore:pressed {
    background-color: @pressed@;
    border-color: @accent@;
}
/* === Library dock === */
QWidget#libraryPanel {
    background-color: @window@;
}
/* Models / IRs / Presets: Union's tabs. Closed ones in the tab tone over a frame line,
   the outer corners rounded; hovered, tinted toward the accent; the open one in the
   window colour, framed, with the accent across its top and open at the bottom. */
QToolButton#librarySection {
    color: @text@;
    background-color: @tab@;
    border: none;
    border-bottom: 1px solid @button_frame@;
    padding: 6px;
}
QToolButton#librarySection[tabPosition="first"] {
    border-top-left-radius: 5px;
}
QToolButton#librarySection[tabPosition="last"] {
    border-top-right-radius: 5px;
}
QToolButton#librarySection:hover, QToolButton#librarySection:pressed {
    background-color: @pressed@;
}
QToolButton#librarySection:checked {
    background-color: @window@;
    border: 1px solid @button_frame@;
    border-top: 3px solid @accent@;
    border-bottom-color: @window@;
    border-top-left-radius: 5px;
    border-top-right-radius: 5px;
}
/* All / ★ / Recent, Folders…, rescan: filled like the preset bar's buttons;
   the active mode checked like A/B. */
QToolButton#libraryMode, QToolButton#libraryTool {
    color: @text@;
    background-color: @button@;
    border: 1px solid @button_frame@;
    border-radius: 5px;
    padding: 3px 9px;
}
QToolButton#libraryMode:hover, QToolButton#libraryTool:hover {
    border-color: @accent@;
}
QToolButton#libraryMode:pressed, QToolButton#libraryTool:pressed {
    background-color: @pressed@;
    border-color: @accent@;
}
QToolButton#libraryMode:checked {
    background-color: @checked@;
}
QLabel#libraryStatus {
    color: @text_muted@;
    padding-left: 4px;
}
)";

// The standard widgets drawn Union's way (KDE Plasma 6.8's look) — applied only when the
// style is Fusion: under a forced theme, or where the desktop's own style isn't available
// (the AppImage, GNOME). A native style (Union, Breeze, Windows) draws them itself.
static const char* const FUSION_SHEET = R"(
/* === Keyboard focus (UnionPolish sets visualFocus; the outline is a FocusRing) === */
/* No focus frame inside the control (Fusion still draws one in styled buttons, check
   boxes and combo boxes): Union's only indicator is the border and the ring. */
QAbstractButton, QComboBox, QAbstractSpinBox, QLineEdit {
    outline: none;
}
*[visualFocus="true"] {
    border-color: @accent@;
}
QCheckBox[visualFocus="true"]::indicator {
    border-color: @accent@;
}
/* The ID-styled buttons below outrank the rule above; they get it by name. */
QToolButton#presetName[visualFocus="true"], QToolButton#presetTool[visualFocus="true"],
QToolButton#presetSlot[visualFocus="true"], QToolButton#presetAb[visualFocus="true"],
QToolButton#menuTool[visualFocus="true"], QToolButton#metroPlay[visualFocus="true"],
QToolButton#heroArrow[visualFocus="true"], QToolButton#libraryMode[visualFocus="true"],
QToolButton#libraryTool[visualFocus="true"], QLineEdit#librarySearch[visualFocus="true"] {
    border-color: @accent@;
}
/* Union's (Breeze): the Header colour (the window's while the window is inactive), no
   line under it; items hovered with the accent tint and an accent border, the open one
   filled. */
QMenuBar {
    background-color: @header@;
    color: @text@;
}
QMenuBar[inactive="true"] {
    background-color: @window@;
}
QMenuBar::item {
    background: transparent;
    padding: 3px 4px;
    border: 1px solid transparent;
    border-radius: 5px;
}
QMenuBar::item:selected {
    background-color: @header_hover@;
    border-color: @accent@;
}
QMenuBar::item:pressed {
    background-color: @accent@;
    border-color: @accent@;
    color: @highlighted_text@;
}
/* Union's menus: rounded, framed, 32 px items rounded inside a 4 px padding; the text
   leaves the column for icons and check marks (6 px padding, 16 px icon, 4 px spacing).
   The corners need a translucent window (apply() gives every menu one). */
/* The menu's body (window colour, frame, 5 px corners) and its shadow are painted by
   UnionPolish; here only the 4 px padding inside the 1 px frame. */
QMenu {
    background: transparent;
    color: @text@;
    border: none;
    padding: 5px;
}
QMenu::item {
    padding: 6px 24px 6px 25px;
    border: 1px solid transparent;
    border-radius: 5px;
}
QMenu[icons="true"]::item {
    padding-left: 6px;
}
QMenu::item:selected {
    background-color: @menu_hover@;
    border-color: @accent@;
}
QMenu::item:disabled {
    color: @disabled_text@;
    background: transparent;
    border-color: transparent;
}
QMenu::indicator, QMenu::icon {
    left: 6px;
}
QMenu::separator {
    height: 1px;
    background: @frame@;
    margin: 4px;
}
/* === Buttons === */
/* Union's buttons: the palette's button colour as is, framed with it 20 % toward the
   text, 5 px corners. Hover puts the accent on the border, the fill unchanged; pressed
   tints the fill toward the accent. Every button below follows it. */
QPushButton {
    background-color: @button@;
    color: @text@;
    border: 1px solid @button_frame@;
    border-radius: 5px;
    padding: 6px 14px;
    min-height: 20px;
    font-weight: 500;
}
QPushButton:hover {
    border-color: @accent@;
}
QPushButton:pressed {
    background-color: @pressed@;
    border-color: @accent@;
}
QPushButton:disabled {
    background-color: @disabled_bg@;
    color: @disabled_text@;
    border-color: @disabled_border@;
}
/* Union's: a 20 px track with a frame line on its inner edge and an 8 px pill (6 px from
   the outer edge, 5 px from the line); hovered or dragged, tinted toward the accent with
   an accent border. */
QScrollBar:vertical {
    background: transparent;
    width: 20px;
    margin: 0;
    border: none;
    border-left: 1px solid @frame@;
}
QScrollBar:horizontal {
    background: transparent;
    height: 20px;
    margin: 0;
    border: none;
    border-top: 1px solid @frame@;
}
QScrollBar::handle {
    background: @bar@;
    border: 1px solid @frame@;
    border-radius: 5px;
}
QScrollBar::handle:vertical {
    min-height: 24px;
    margin: 6px 6px 6px 5px;
}
QScrollBar::handle:horizontal {
    min-width: 24px;
    margin: 5px 6px 6px 6px;
}
QScrollBar::handle:hover, QScrollBar::handle:pressed {
    background: @bar_hover@;
    border-color: @accent@;
}
QScrollBar::add-line, QScrollBar::sub-line {
    width: 0;
    height: 0;
    background: none;
    border: none;
}
QScrollBar::add-page, QScrollBar::sub-page {
    background: none;
}
/* === Status bar (bottom, VS Code style) === */
QStatusBar {
    background-color: @window@;
    border-top: 1px solid @frame@;
    min-height: 24px;
}
QStatusBar::item {
    border: none;
}
QLineEdit#numberEdit {
    color: @accent@;
    border-color: @accent@; /* shown only while typing a value */
    padding: 0;
    font-size: @pt:12@;
    font-weight: 700;
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
QDialog#midiMappings QToolButton {
    color: @text@;
    background-color: @button@;
    border: 1px solid @button_frame@;
    border-radius: 5px;
    padding: 2px 8px;
}
QDialog#midiMappings QToolButton:hover {
    border-color: @accent@;
}
QDialog#midiMappings QToolButton:pressed {
    background-color: @pressed@;
    border-color: @accent@;
}
QLineEdit#librarySearch {
    padding: 6px;
}
/* === Dialogs inside the window tree (Library Folders) === */
/* Union's lists: the view background in a frame; rows inset 2 px and rounded, hovered
   with the accent tint and an accent border, selected in the accent with its text colour.
   (The library list paints its rows the same way, in LibraryItemDelegate.) */
QListView {
    background-color: @view@;
    color: @text@;
    border: 1px solid @view_frame@;
    border-radius: 5px;
    outline: none;
}
QListView::item {
    margin: 2px;
    padding: 4px;
    min-height: 22px;
    border: 1px solid transparent;
    border-radius: 5px;
}
QListView::item:hover {
    background-color: @list_hover@;
    border-color: @accent@;
}
QListView::item:selected {
    background-color: @accent@;
    border-color: @accent@;
    color: @highlighted_text@;
}
/* The Settings sidebar: Union's side panel — no frame of its own, a line on its right. */
QListView#settingsSidebar {
    border: none;
    border-right: 1px solid @frame@;
    border-radius: 0;
}
/* Tables: the same states, square. */
QTableView {
    background-color: @view@;
    outline: none;
}
QTableView::item:hover {
    background-color: @list_hover@;
}
QTableView::item:selected {
    background-color: @accent@;
    color: @highlighted_text@;
}
/* Union's tabs, as on the library's sections. */
QTabWidget::pane {
    border: 1px solid @button_frame@;
    top: -1px;
}
QTabBar::tab {
    color: @text@;
    background-color: @tab@;
    border: none;
    border-bottom: 1px solid @button_frame@;
    padding: 6px 14px;
}
QTabBar::tab:first {
    border-top-left-radius: 5px;
}
QTabBar::tab:last {
    border-top-right-radius: 5px;
}
QTabBar::tab:hover {
    background-color: @pressed@;
}
QTabBar::tab:selected {
    background-color: @window@;
    border: 1px solid @button_frame@;
    border-top: 3px solid @accent@;
    border-bottom-color: @window@;
    border-top-left-radius: 5px;
    border-top-right-radius: 5px;
}
/* === Text fields === */
/* Union's: the view background framed with its frame colour, 5 px corners, the accent on
   the border when hovered or typing. A spin box's own field is bare: the spin box is the
   field. */
QLineEdit {
    background-color: @view@;
    color: @text@;
    border: 1px solid @view_frame@;
    border-radius: 5px;
    padding: 4px 6px;
    selection-background-color: @accent@;
    selection-color: @highlighted_text@;
}
QLineEdit:hover, QLineEdit:focus {
    border-color: @accent@;
}
QAbstractSpinBox QLineEdit {
    background: transparent;
    border: none;
    padding: 0;
}
/* === Check boxes, spin boxes, table headers (Settings, MIDI Mappings) === */
/* Union's check box: a 16 px rounded box in the button colour; checked, tinted toward the
   accent with an accent border and a check mark. The same box marks checkable list rows
   and menu items; a menu's exclusive items get the round one with a dot. */
QCheckBox {
    spacing: 4px;
}
QCheckBox::indicator, QListView::indicator, QMenu::indicator {
    width: 14px;
    height: 14px;
    background-color: @card@;
    border: 1px solid @button_frame@;
    border-radius: 5px;
}
QCheckBox::indicator:hover, QListView::indicator:hover {
    border-color: @accent@;
}
QCheckBox::indicator:checked, QListView::indicator:checked, QMenu::indicator:checked {
    background-color: @pressed@;
    border-color: @accent@;
    image: url(@check@);
}
QMenu::indicator:exclusive {
    border-radius: 8px;
}
QMenu::indicator:exclusive:checked {
    image: url(@radio@);
}
QCheckBox::indicator:disabled, QListView::indicator:disabled {
    background-color: @disabled_bg@;
    border-color: @disabled_border@;
}
QCheckBox::indicator:checked:disabled, QListView::indicator:checked:disabled {
    image: url(@check_disabled@);
}
/* Union's spin box: a field with stacked chevrons, the accent on the border when hovered
   or focused. A style-sheet box makes Fusion drop its arrows, so they are given here; the
   text's left margin is set in UnionPolish. */
QAbstractSpinBox {
    background-color: @view@;
    color: @text@;
    border: 1px solid @view_frame@;
    border-radius: 5px;
    padding: 3px 4px;
    min-height: 20px;
    selection-background-color: @accent@;
    selection-color: @highlighted_text@;
}
QAbstractSpinBox:hover, QAbstractSpinBox:focus {
    border-color: @accent@;
}
QAbstractSpinBox:disabled {
    color: @disabled_text@;
    border-color: @disabled_border@;
}
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button {
    subcontrol-origin: border;
    width: 20px;
    border: none;
    background: transparent;
}
QAbstractSpinBox::up-button {
    subcontrol-position: top right;
}
QAbstractSpinBox::down-button {
    subcontrol-position: bottom right;
}
QAbstractSpinBox::up-arrow {
    image: url(@arrow_up@);
    width: 12px;
    height: 12px;
}
QAbstractSpinBox::down-arrow {
    image: url(@arrow@);
    width: 12px;
    height: 12px;
}
QAbstractSpinBox::up-arrow:disabled, QAbstractSpinBox::up-arrow:off {
    image: url(@arrow_up_disabled@);
}
QAbstractSpinBox::down-arrow:disabled, QAbstractSpinBox::down-arrow:off {
    image: url(@arrow_disabled@);
}
/* Union's header: the button colour, framed right and below, plain centred text. */
QHeaderView::section {
    background-color: @card@;
    color: @text@;
    border: none;
    border-right: 1px solid @button_frame@;
    border-bottom: 1px solid @button_frame@;
    padding: 4px;
    min-height: 22px;
}
/* === Combo boxes (library sort) === */
QComboBox {
    background-color: @button@;
    color: @text@;
    border: 1px solid @button_frame@;
    border-radius: 5px;
    padding: 4px 8px;
    min-height: 20px;
    font-weight: 500;
    combobox-popup: 0; /* a drop-down list below the box, as Union opens it (Fusion: a menu over it) */
}
QComboBox:hover {
    border-color: @accent@;
}
QComboBox:disabled {
    background-color: @disabled_bg@;
    color: @disabled_text@;
    border-color: @disabled_border@;
}
/* Union's chevron, 6 px from the border. */
QComboBox::drop-down {
    border: none;
    width: 22px;
}
QComboBox::down-arrow {
    image: url(@arrow@); /* @2x picked up on HiDPI */
    width: 16px;
    height: 16px;
    margin-right: 6px;
}
QComboBox::down-arrow:disabled {
    image: url(@arrow_disabled@);
}
/* The drop-down: Union's popup — the view background, framed and rounded — with rows
   drawn like menu items (8 px padding); the row under the mouse is the current one, and
   it shows the menu's hover: the accent tint and an accent border. */
QComboBoxPrivateContainer {
    background: transparent;
    border: none;
}
QComboBox QAbstractItemView {
    background-color: @view@;
    color: @text@;
    border: 1px solid @frame@;
    border-radius: 5px;
    padding: 0 3px;
    outline: none;
}
QComboBox QAbstractItemView::item {
    padding: 8px;
    border: 1px solid transparent;
    border-radius: 5px;
}
QComboBox QAbstractItemView::item:hover, QComboBox QAbstractItemView::item:selected {
    background-color: @list_hover@;
    border-color: @accent@;
    color: @text@;
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

// Union's small icons, painted in `color` into the cache folder (style sheets take only
// files) at 16 px with an @2x twin; the URL is empty when that fails.
//   chevron-down / chevron-up: breeze-icons' arrow-down-symbolic (combo boxes, menu
//     buttons, spin boxes), its path as the SVG has it;
//   check: Union's checkmark.svg, a 2.5 px stroke; radio: Union's radio.svg, a 3 px dot.
static QString icon_url(const QString& name, const QColor& color) {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString base = QString("%1-%2").arg(name, color.name().mid(1));
    const QString path = dir + '/' + base + ".png";
    if (QFileInfo::exists(path) && QFileInfo::exists(dir + '/' + base + "@2x.png")) {
        return path;
    }
    QDir().mkpath(dir);
    const QPolygonF chevron(
        {{8, 11.707}, {2, 5.707}, {2.707, 5}, {8, 10.293}, {13.293, 5}, {14, 5.707}});
    for (const int scale : {1, 2}) {
        QImage image(16 * scale, 16 * scale, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(scale, scale);
        if (name == "check") {
            p.setPen(QPen(color, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::MiterJoin));
            p.drawPolyline(QPolygonF({{4, 8}, {7, 11}, {13, 5}}));
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            if (name == "radio") {
                p.drawEllipse(QPointF(8, 8), 3, 3);
            } else if (name == "chevron-up") {
                p.drawPolygon(QTransform(1, 0, 0, -1, 0, 16).map(chevron));
            } else {
                p.drawPolygon(chevron);
            }
        }
        p.end();
        if (!image.save(dir + '/' + base + (scale == 2 ? "@2x" : "") + ".png")) {
            return {};
        }
    }
    return path;
}

// The system font's size relative to the 9 pt the layout was drawn for.
static qreal font_scale() {
    const qreal body = QApplication::font().pointSizeF();
    return body > 0 ? body / 9.0 : 1.0; // a pixel-sized font: as drawn
}

static QString fill_roles(const char* sheet) {
    const Colors& pal = palette();
    const std::pair<const char*, QColor> roles[] = {
        {"window", pal.window},
        {"surface", pal.surface},
        {"card", pal.card},
        {"button", pal.card}, // the palette's button colour, as buttons use it
        {"raised", pal.raised},
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
        {"header", pal.header},
        {"frame", pal.frame},
        {"header_hover", pal.header_hover},
        {"menu_hover", pal.menu_hover},
        {"button_frame", pal.button_frame},
        {"tab", pal.tab},
        {"checked", pal.checked},
        {"bar", pal.bar},
        {"bar_hover", pal.bar_hover},
        {"view", pal.view},
        {"view_frame", pal.view_frame},
        {"list_hover", pal.list_hover},
        {"highlighted_text", pal.highlighted_text},
        {"accent", current_accent},
    };
    QString qss = QString::fromUtf8(sheet);
    for (const auto& [role, color] : roles) {
        qss.replace(QString("@%1@").arg(QLatin1String(role)), color.name());
    }
    // Text larger than the body (@pt:N@, N as drawn at a 9 pt body) grows with the
    // system font, which the body text follows. Glyphs in fixed boxes (≡, ‹ ›, ▾, ▶)
    // keep their size, like icons.
    static const QRegularExpression SIZE("@pt:(\\d+)@");
    for (QRegularExpressionMatch m = SIZE.match(qss); m.hasMatch(); m = SIZE.match(qss)) {
        const qreal pt = m.captured(1).toInt() * font_scale();
        qss.replace(m.capturedStart(), m.capturedLength(), QString::number(pt, 'f', 1) + "pt");
    }
    // Icons in the text colour, and in the disabled text colour for disabled controls.
    struct Icon
    {
        const char* placeholder;
        const char* name;
        QColor color;
    };
    const Icon icons[] = {
        {"@arrow@", "chevron-down", pal.text},
        {"@arrow_disabled@", "chevron-down", pal.disabled_text},
        {"@arrow_up@", "chevron-up", pal.text},
        {"@arrow_up_disabled@", "chevron-up", pal.disabled_text},
        {"@check@", "check", pal.text},
        {"@check_disabled@", "check", pal.disabled_text},
        {"@radio@", "radio", pal.text},
        {"@radio_disabled@", "radio", pal.disabled_text},
    };
    for (const Icon& icon : icons) {
        if (qss.contains(QLatin1String(icon.placeholder))) {
            qss.replace(QLatin1String(icon.placeholder), icon_url(icon.name, icon.color));
        }
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
    // Theme files don't name it: white or the text, whichever reads better on the accent.
    const QColor white(255, 255, 255);
    p.setColor(QPalette::HighlightedText,
               contrast(spec.accent, white) >= contrast(spec.accent, spec.text) ? white
                                                                                : spec.text);
    p.setColor(QPalette::Shadow, spec.shadow);
    p.setColor(QPalette::Disabled, QPalette::WindowText, spec.disabled_text);
    return p;
}

// The menu bar's colour while following the system: KDE's Header colour, read from
// kdeglobals — Qt's palette has no such role — when that file describes the palette in use
// (its window colour matches); elsewhere (Windows, GNOME) the window's.
static QColor system_header(const QPalette& system) {
    const QColor window = system.color(QPalette::Window);
    const QString path =
        QStandardPaths::locate(QStandardPaths::GenericConfigLocation, "kdeglobals");
    if (!path.isEmpty()) {
        const auto kde = load_kde_colors(path);
        if (kde && kde->window.rgb() == window.rgb()) {
            return kde->header;
        }
    }
    return window;
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
    const QColor header = current_mode == Mode::SYSTEM   ? system_header(system)
                          : current_mode == Mode::CUSTOM ? custom_theme.header
                                                         : breeze_header(dark_active);
    current_accent = source.color(QPalette::Highlight);
    active = from_palette(source, dark_active, header);
    // Breeze's icons come in a light and a dark set and Qt doesn't recolour them: under a
    // forced theme the set has to match NAM-X's, not the desktop's.
    const QString icons = QIcon::themeName();
    if (icons == "breeze" || icons == "breeze-dark") {
        QIcon::setThemeName(dark_active ? "breeze-dark" : "breeze");
    }
}

// Follows the OS while Mode::SYSTEM: a scheme switch (Qt 6.5+) or a new system
// palette (an accent change; the only signal before 6.5). A new system font in every
// mode: the larger text sizes are derived from it.
class SystemThemeWatcher : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        const bool palette_change =
            event->type() == QEvent::ApplicationPaletteChange && current_mode == Mode::SYSTEM;
        if (watched == qApp && (palette_change || event->type() == QEvent::ApplicationFontChange)) {
            QTimer::singleShot(0, qApp, [] {
                reapply();
            });
        }
        return QObject::eventFilter(watched, event);
    }
};

// Union's menu body and shadow (box-shadow: 0 2px 20px rgba(0, 0, 0, 0.2)). The window
// is MENU_SHADOW larger right and below (contents margins, which QMenu counts per side;
// a stylesheet margin it counts on all four) and the shadow is painted there. The menu's
// top-left corner stays where Qt and Wayland anchor it — to the menu-bar item, or the
// parent's row — so the fringe above and left of Union's shadow is left out.
static constexpr int MENU_SHADOW = 20;

static void paint_menu_body(QWidget* menu) {
    QPainter p(menu);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF body = QRectF(menu->rect()).adjusted(0, 0, -MENU_SHADOW, -MENU_SHADOW);
    // The shadow: its shape 2 px down, then one ring per pixel outward, fading as a blur
    // of 20 px does (σ ≈ 7).
    const QRectF shadow = body.translated(0, 2);
    QPainterPath shape;
    shape.addRoundedRect(shadow, 5, 5);
    p.fillPath(shape, QColor(0, 0, 0, static_cast<int>(std::lround(0.2 * 255))));
    for (int i = 1; i <= MENU_SHADOW; ++i) {
        const double fade = std::exp(-(i * i) / (2.0 * 7.0 * 7.0));
        p.setPen(QPen(QColor(0, 0, 0, static_cast<int>(std::lround(0.2 * 255 * fade))), 1.0));
        p.drawRoundedRect(shadow.adjusted(-i + 0.5, -i + 0.5, i - 0.5, i - 0.5), 5 + i, 5 + i);
    }
    // The body: the window colour, framed.
    p.setPen(QPen(active.frame, 1.0));
    p.setBrush(active.window);
    p.drawRoundedRect(body.adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
}

// Union's keyboard focus outline: 2 px of the accent at 30 % around the control (around
// its box, for a check box). A stylesheet can't draw outside a widget, so this ring sits
// on the control's parent, following it, until the focus leaves.
class FocusRing : public QWidget
{
public:
    explicit FocusRing(QWidget* new_target)
        : QWidget(new_target->parentWidget())
        , target(new_target) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        target->installEventFilter(this);
        follow();
        show();
        raise();
    }

protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
            follow();
        } else if (event->type() == QEvent::Hide) {
            hide();
        }
        return false;
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QColor ring = current_accent;
        ring.setAlphaF(0.3f);
        p.setPen(QPen(ring, 2));
        p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 6, 6);
    }

private:
    void follow() {
        QRect area = target->geometry();
        if (auto* box = qobject_cast<QCheckBox*>(target.data())) {
            QStyleOptionButton option;
            option.initFrom(box);
            const QRect indicator =
                box->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, box);
            area = QRect(box->mapTo(parentWidget(), indicator.topLeft()), indicator.size());
        }
        setGeometry(area.adjusted(-2, -2, 2, 2));
    }

    QPointer<QWidget> target;
};

// The control showing keyboard focus: its `visualFocus` property turns its border to the
// accent (the stylesheet), and the ring surrounds it.
static QPointer<QWidget> focus_target;
static QPointer<FocusRing> focus_ring;

static void show_visual_focus(QWidget* target) {
    if (focus_target) {
        focus_target->setProperty("visualFocus", false);
        focus_target->style()->unpolish(focus_target);
        focus_target->style()->polish(focus_target);
    }
    delete focus_ring;
    focus_target = target;
    if (!target || !target->parentWidget()) {
        return;
    }
    target->setProperty("visualFocus", true);
    target->style()->unpolish(target);
    target->style()->polish(target);
    focus_ring = new FocusRing(target);
}

// The controls Union outlines on keyboard focus: buttons, check boxes, combo boxes, spin
// boxes and text fields (a spin box's own field counts as the spin box).
static QWidget* focus_control(QObject* object) {
    auto* widget = qobject_cast<QWidget*>(object);
    if (qobject_cast<QLineEdit*>(widget) &&
        qobject_cast<QAbstractSpinBox*>(widget->parentWidget())) {
        return widget->parentWidget();
    }
    if (qobject_cast<QAbstractButton*>(widget) || qobject_cast<QComboBox*>(widget) ||
        qobject_cast<QAbstractSpinBox*>(widget) || qobject_cast<QLineEdit*>(widget)) {
        return widget;
    }
    return nullptr;
}

// What the stylesheet can't do, done to each widget as it is polished:
// - every menu, context menus included, gets a translucent window (before its window
//   exists), so the rounded corners show what is behind them — as Breeze does — room
//   right and below for its shadow, and its body and shadow painted (paint_menu_body);
// - a spin box's text keeps Union's 8 px from the border: the stylesheet's padding doesn't
//   reach the spin box's inner field, its text margins do (2 px are the field's own);
// - an item view lays its rows out again once polished: rows added before the stylesheet
//   reached it (the Settings sidebar) kept the plain style's height;
// - a combo box paints its drop-down rows with the standard delegate — its own ignores
//   the stylesheet's row rules — and the drop-down's window is translucent, so its
//   corners can be rounded like a menu's.
// And Union's keyboard focus (FocusRing): shown when Tab brings the focus to a control,
// gone when the focus leaves it — a click gives focus without the outline.
// Whether a menu has icons (the stylesheet's QMenu[icons]); re-polished when it changes.
static void update_menu_icons(QMenu* menu) {
    const QList<QAction*> actions = menu->actions();
    const bool icons = std::any_of(actions.begin(), actions.end(), [](QAction* a) {
        return !a->icon().isNull() && a->isIconVisibleInMenu();
    });
    if (menu->property("icons").toBool() != icons) {
        menu->setProperty("icons", icons);
        menu->style()->unpolish(menu);
        menu->style()->polish(menu);
    }
}

// The per-widget part of Fusion's Union-like look, or — under a native style — taking it
// back. Run as each widget is polished, and on every existing widget when the style
// changes live (QApplication::setStyle re-polishes through the style alone, without the
// Polish event the filter sees).
static void adjust_for_style(QWidget* widget) {
    if (!fusion_active()) {
        if (auto* menu = qobject_cast<QMenu*>(widget)) {
            menu->setContentsMargins(0, 0, 0, 0);
            QEvent restyled(QEvent::StyleChange); // measured again, under this style
            QCoreApplication::sendEvent(menu, &restyled);
        } else if (auto* spin = qobject_cast<QAbstractSpinBox*>(widget)) {
            if (auto* field = spin->findChild<QLineEdit*>()) {
                field->setTextMargins(0, 0, 0, 0);
            }
        }
        return;
    }
    if (auto* menu = qobject_cast<QMenu*>(widget)) {
        if (!menu->testAttribute(Qt::WA_TranslucentBackground)) {
            menu->setAttribute(Qt::WA_TranslucentBackground);
            // A window's transparency is fixed when it is created: a menu that has been
            // shown already gets a new window (it is hidden now; setParent recreates it).
            if (menu->testAttribute(Qt::WA_WState_Created)) {
                menu->setParent(menu->parentWidget(), menu->windowFlags());
            }
        }
        menu->setContentsMargins(0, 0, MENU_SHADOW, MENU_SHADOW);
        // It measures its items once and keeps them: measured again, under this style.
        QEvent restyled(QEvent::StyleChange);
        QCoreApplication::sendEvent(menu, &restyled);
        // Qt adds an icon column of its own once a menu has icons: the stylesheet then
        // pads less (QMenu[icons]), so the text stays in Union's place. Settled now and
        // before each showing (actions come and go) — before the menu measures itself.
        update_menu_icons(menu);
        if (!menu->property("iconWatch").toBool()) {
            menu->setProperty("iconWatch", true);
            QObject::connect(menu, &QMenu::aboutToShow, menu, [menu] {
                update_menu_icons(menu);
            });
        }
    } else if (auto* spin = qobject_cast<QAbstractSpinBox*>(widget)) {
        if (auto* field = spin->findChild<QLineEdit*>()) {
            field->setTextMargins(6, 0, 0, 0);
        }
    } else if (auto* view = qobject_cast<QAbstractItemView*>(widget)) {
        QTimer::singleShot(0, view, [view] {
            view->doItemsLayout();
        });
    } else if (auto* combo = qobject_cast<QComboBox*>(widget)) {
        if (!qobject_cast<QStyledItemDelegate*>(combo->itemDelegate())) {
            combo->setItemDelegate(new QStyledItemDelegate(combo));
        }
    } else if (widget->inherits("QComboBoxPrivateContainer")) {
        auto* container = static_cast<QFrame*>(widget);
        container->setFrameShape(QFrame::NoFrame);
        container->setAttribute(Qt::WA_TranslucentBackground);
    }
}

class UnionPolish : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Polish && watched->isWidgetType()) {
            adjust_for_style(static_cast<QWidget*>(watched));
            return QObject::eventFilter(watched, event);
        }
        if (!fusion_active()) {
            if (focus_target) {
                show_visual_focus(nullptr);
            }
            return QObject::eventFilter(watched, event);
        }
        if (event->type() == QEvent::Paint && qobject_cast<QMenu*>(watched)) {
            paint_menu_body(static_cast<QWidget*>(watched)); // under the menu's own items
        } else if (event->type() == QEvent::FocusIn) {
            const Qt::FocusReason reason = static_cast<QFocusEvent*>(event)->reason();
            if (QWidget* control = focus_control(watched);
                control && control != focus_target &&
                (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason)) {
                show_visual_focus(control);
            }
        } else if (event->type() == QEvent::FocusOut && focus_target &&
                   focus_control(watched) == focus_target) {
            show_visual_focus(nullptr);
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
    native_style = app.style()->name(); // no stylesheet yet: the style itself
    active_style = native_style;
    choose_style();
    app.setStyleSheet(fusion_active() ? fill_roles(APP_STYLE_SHEET) : QString());
    app.installEventFilter(new SystemThemeWatcher(&app));
    app.installEventFilter(new UnionPolish(&app));
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
    p.setColor(QPalette::Base, QColor(pal.view));
    // Union's placeholder: the text 30 % toward the field.
    p.setColor(QPalette::PlaceholderText, mix_colors(pal.text, pal.view, 0.3));
    p.setColor(QPalette::AlternateBase, QColor(pal.card));
    p.setColor(QPalette::ToolTipBase, QColor(pal.raised));
    p.setColor(QPalette::ToolTipText, QColor(pal.text));
    p.setColor(QPalette::Text, QColor(pal.text));
    p.setColor(QPalette::Button, QColor(pal.card)); // the palette's button colour (Union's)
    p.setColor(QPalette::ButtonText, QColor(pal.text));
    p.setColor(QPalette::BrightText, QColor("#ffffff"));
    p.setColor(QPalette::Mid, QColor(pal.border));
    p.setColor(QPalette::Dark, QColor(pal.window));
    p.setColor(QPalette::Shadow, QColor(pal.shadow));
    p.setColor(QPalette::Highlight, current_accent);
    // Text on the accent: the scheme's own (Breeze's white), as KDE draws it.
    p.setColor(QPalette::HighlightedText, QColor(pal.highlighted_text));

    p.setColor(QPalette::Disabled, QPalette::Text, QColor(pal.disabled_text));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(pal.disabled_text));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(pal.disabled_text));

    root.setPalette(p);

    // Per-effect accent rules — the palette hex verbatim (dark) for the name label
    // and the loaded-file label on the Amplifier/Cabinet rows (knob rings and
    // toggles get theirs pushed by tagCard).
    QString qss = fill_roles(STYLE_SHEET);
    if (fusion_active()) {
        qss += fill_roles(FUSION_SHEET);
    }
    for (const auto& fc : FX_COLORS) {
        qss += QString::fromUtf8("QFrame[fx=\"%1\"] QLabel#fxName { color: %2; }\n"
                                 "QFrame[fx=\"%1\"] QLabel#fileStatusOk { color: %2; }\n")
                   .arg(QLatin1String(fc.id), readable_accent(accent(fc.id)).name());
    }
    root.setStyleSheet(qss);
}

void reapply(bool force) {
    // Restyling sends events (palette, style changes) that could ask for it again.
    static bool reapplying = false;
    if (reapplying) {
        return;
    }
    reapplying = true;
    const auto done = qScopeGuard([] {
        reapplying = false;
    });
    const QColor old_accent = current_accent;
    const QString old_sheet = fill_roles(STYLE_SHEET);
    resolve_scheme();
    if (!force && old_sheet == fill_roles(STYLE_SHEET)) {
        return; // the event was ours (or changed nothing we use): no loop, no flicker
    }
    const bool restyled = choose_style();
    qApp->setStyleSheet(fusion_active() ? fill_roles(APP_STYLE_SHEET) : QString());
    if (!themed_root) {
        return;
    }
    // A new style live: the window's stylesheet is rebuilt on it from scratch (Qt layers a
    // sheet over the style in use when it is set), and every widget gets the new style's
    // adjustments — the re-polish didn't pass through the polish filter.
    if (restyled) {
        themed_root->setStyleSheet(QString());
    }
    apply_to(*themed_root);
    if (restyled) {
        for (QWidget* widget : QApplication::allWidgets()) {
            adjust_for_style(widget);
        }
    }
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

// A theme picked in Settings is applied on the next turn of the event loop, never from
// inside the click that picked it: the switch can change the application's style, and
// the drop-down still handling that click would lose its style objects mid-event (a
// segfault in QApplication::setStyleSheet, 2026-10-24). The mode is recorded at once.
static void reapply_soon() {
    QTimer::singleShot(0, qApp, [] {
        reapply(true);
    });
}

void set_mode(Mode new_mode) {
    assert(new_mode != Mode::CUSTOM);
    current_mode = new_mode;
    QSettings().setValue("ui/theme", static_cast<int>(new_mode));
    watch_custom_file();
    reapply_soon();
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
    reapply_soon();
    return {};
}

QString custom_path() {
    return current_mode == Mode::CUSTOM ? custom_theme_path : QString();
}

} // namespace Theme

} // namespace namx
