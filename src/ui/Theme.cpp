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
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QRegularExpression>
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
    QColor pressed; // pressed buttons: tinted toward the accent
    QColor line; // separators and quiet borders
    QColor border; // control borders
    QColor disabled_bg;
    QColor disabled_text;
    QColor text;
    QColor text_dim; // secondary labels
    QColor text_muted; // status and captions
    QColor text_faint; // grips, captions
    QColor text_off; // empty preset slots
    QColor notice; // status bar problems
    QColor shadow;
    QColor button_frame; // check boxes, table headers: the button 20 % toward the text
    QColor tab; // closed tabs: the view background 17 % toward the text (Union's)
    QColor checked; // checked buttons: the button 12.5 % toward the text (Union's)
    QColor view; // lists, fields: the palette's base, as is (Union's view background)
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

// Every chrome tone from a palette (the OS's, or Breeze's when forced): the window, base,
// button, text, shadow and highlight colours as they are, the in-between tones mixed
// between them.
// Mixing toward the text colour darkens on a light scheme and lightens on a dark one,
// so one recipe serves both. Ours stay: the effect accents, the LED colours and the
// status bar's warning red.
static Colors from_palette(const QPalette& sys, bool dark) {
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
        mix_colors(button, accent, 1.0 / 3), // pressed
        mix_colors(w, text, 0.12), // line
        border,
        mix_colors(w, button, 0.5), // disabled_bg
        sys.color(QPalette::Disabled, QPalette::WindowText),
        text,
        mix_colors(text, w, 0.22), // text_dim
        mix_colors(text, w, 0.32), // text_muted
        mix_colors(text, w, 0.45), // text_faint
        mix_colors(text, w, 0.58), // text_off
        QColor(ours.notice),
        sys.color(QPalette::Shadow),
        mix_colors(button, text, 0.2), // button_frame
        mix_colors(base, text, 0.17), // tab
        mix_colors(button, text, 0.125), // checked
        base, // view
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
/* === Scroll area === */
QScrollArea {
    background: transparent;
    border: none;
}
/* The rack itself (through the viewport — not the scroll bars, which Union draws). */
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
/* === Status bar (bottom, VS Code style) === */
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
QToolButton#heroArrow {
    color: @text@;
    font-size: 15pt;
    font-weight: 600;
    min-width: 22px;
    max-width: 22px;
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
QLineEdit#numberEdit {
    color: @accent@;
    border-color: @accent@; /* shown only while typing a value */
    padding: 0;
    font-size: @pt:12@;
    font-weight: 700;
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

// The ▾ of NAM-X's menu buttons (preset name, 4/4, Click…): Union's chevron —
// breeze-icons' arrow-down-symbolic, its path as the SVG has it — in `color`, painted into
// the cache folder (style sheets take only files) at 16 px with an @2x twin; the URL is
// empty when that fails.
static QString chevron_url(const QColor& color) {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString base = QString("chevron-down-%1").arg(color.name().mid(1));
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
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPolygon(chevron);
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
        {"pressed", pal.pressed},
        {"line", pal.line},
        {"disabled_bg", pal.disabled_bg},
        {"text", pal.text},
        {"text_dim", pal.text_dim},
        {"text_muted", pal.text_muted},
        {"text_faint", pal.text_faint},
        {"text_off", pal.text_off},
        {"notice", pal.notice},
        {"button_frame", pal.button_frame},
        {"tab", pal.tab},
        {"checked", pal.checked},
        {"accent", current_accent},
        {"on_accent", on_accent()},
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
    if (qss.contains("@arrow@")) {
        qss.replace("@arrow@", chevron_url(pal.text));
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
    // Union (KDE Plasma 6.8's style engine) draws every standard control; the stylesheet
    // only styles NAM-X's own pieces. Union takes its colours from KDE's colour scheme,
    // not from the palette: under a forced theme, its controls keep the desktop's.
    app.setStyle("Union");
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
                   .arg(QLatin1String(fc.id), readable_accent(accent(fc.id)).name());
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
