#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QWidget>

#include "Common.h"
#include "KnobWidget.h"
#include "ToggleSwitch.h"

namespace npl
{

namespace Theme
{

// ---------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------

// Per-effect accents (user-chosen, 2026-08-10). IDs match
// AudioEngine::effect_table; "neutral" covers Input/Output and fallbacks.
struct FxColor
{
    const char* id;
    const char* accent;
};
static constexpr FxColor FX_COLORS[] = {
    {"tuner", "#b6aa8d"},     {"gate", "#8f969e"},    {"comp", "#4f78bd"},    {"wah", "#d4c19a"},
    {"flanger", "#93477b"},   {"phaser", "#4bba5b"},  {"nam", "#e4b954"},     {"eq", "#a5a38f"},
    {"chorus", "#8dccf3"},    {"tremolo", "#19848e"}, {"delay", "#aaa6a0"},   {"reverb", "#8f7d99"},
    {"ir", "#ef661a"},        {"drive", "#d65a5a"},   {"doubler", "#e0a3c2"}, {"volume", "#9fb4c7"},
    {"transpose", "#c9a6f2"}, {"neutral", "#7d8590"},
};

QColor accent(const QString& fx_id) {
    for (const auto& fc : FX_COLORS) {
        if (fx_id == QLatin1String(fc.id)) {
            return QColor(fc.accent);
        }
    }
    return QColor("#7d8590");
}

QColor generic_accent() {
    return QColor("#5cc8a8");
}

// Neutral chrome tones (keep in sync with the stylesheet below).
QColor track() {
    return QColor("#2a2f38");
}
QColor track_hover() {
    return QColor("#353c4a");
}
QColor knob_body_top() {
    return QColor("#2e3441");
}
QColor knob_body_bot() {
    return QColor("#1c2028");
}
QColor bezel() {
    return QColor("#0e1015");
}
QColor well() {
    return QColor("#181b22");
}
QColor dim_text() {
    return QColor("#aab0bc");
}
QColor thumb_top() {
    return QColor("#ffffff");
}
QColor thumb_bot() {
    return QColor("#d8dee9");
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
    return QColor("#42e86c");
}
QColor led_red() {
    return QColor("#ff453a");
}
QColor display_bg() {
    return QColor("#07080c");
}

void tag_card(QWidget* card, const QString& fx_id) {
    if (!card) {
        return;
    }
    card->setProperty("fx", fx_id);

    // The app stylesheet disables parent→child palette propagation, so the
    // accents are pushed to the interactive descendants explicitly. Call
    // tagCard AFTER the card's knobs/toggles have been created.
    const QColor a = accent(fx_id);
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

// ---------------------------------------------------------------------------
// Stylesheet
// ---------------------------------------------------------------------------

static const char* k_style_sheet = R"(
/* === Window / base === */
QMainWindow, QDialog {
    background-color: #15171c;
}
QWidget {
    color: #d8dee9;
    font-size: 9pt;
}

/* === Menu bar === */
QMenuBar {
    background-color: #1a1d24;
    color: #d8dee9;
    border-bottom: 1px solid #2a2f38;
    padding: 2px 4px;
}
QMenuBar::item {
    background: transparent;
    padding: 4px 10px;
    border-radius: 4px;
}
QMenuBar::item:selected {
    background-color: #2f3744;
}
QMenu {
    background-color: #1a1d24;
    color: #d8dee9;
    border: 1px solid #2a2f38;
    padding: 4px 0;
}
QMenu::item {
    padding: 6px 24px;
}
QMenu::item:selected {
    background-color: #2f3744;
}
QMenu::separator {
    height: 1px;
    background: #2a2f38;
    margin: 4px 6px;
}

/* === Buttons === */
QPushButton {
    background-color: #2a2f38;
    color: #d8dee9;
    border: 1px solid #353c4a;
    border-radius: 5px;
    padding: 6px 14px;
    min-height: 20px;
    font-weight: 500;
}
QPushButton:hover {
    background-color: #353c4a;
    border-color: #424b5c;
}
QPushButton:pressed {
    background-color: #1f242c;
}
QPushButton:disabled {
    background-color: #1d2027;
    color: #555c6b;
    border-color: #25292f;
}

/* === Effect row cards === */
QFrame[fxCard="true"] {
    background-color: #1f242c;
    border: 1px solid #2a2f38;
    border-radius: 6px;
}
QLabel#fxGrip {
    color: #7d8590;
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
    background: #15171c;
    height: 10px;
    margin: 0;
    border: none;
}
QScrollBar::handle:horizontal {
    background: #353c4a;
    border-radius: 4px;
    min-width: 24px;
    margin: 1px;
}
QScrollBar::handle:horizontal:hover {
    background: #424b5c;
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
    background: #15171c;
    width: 10px;
    margin: 0;
    border: none;
}
QScrollBar::handle:vertical {
    background: #353c4a;
    border-radius: 4px;
    min-height: 24px;
    margin: 1px;
}
QScrollBar::handle:vertical:hover {
    background: #424b5c;
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
    color: #aab0bc;
}

/* === Status bar (bottom, VS Code style) === */
QStatusBar {
    background-color: #1a1d24;
    border-top: 1px solid #2a2f38;
    min-height: 24px;
}
QStatusBar::item {
    border: none;
}
QLabel#statusItem, QLabel#statusAction {
    color: #9aa1ad;
    padding: 2px 8px;
    border-radius: 4px;
}
QLabel#statusAction:hover {
    background-color: #2f3744;
    color: #d8dee9;
}

/* === NAM/IR status labels (file row) === */
QLabel#fileStatus {
    color: #9aa1ad;
    font-style: italic;
    padding-left: 6px;
}
QLabel#fileStatusOk {
    color: #5cc8a8;
    font-weight: 600;
    padding-left: 6px;
}

/* === Hero rows (Amplifier / Cabinet): metadata + ‹ › arrows === */
QLabel#fileMeta {
    color: #aab0bc;
    padding-left: 8px;
}
QToolButton#heroArrow {
    color: #d8dee9;
    font-size: 15pt;
    font-weight: 600;
    min-width: 22px;
    max-width: 22px;
    padding: 0 0 3px 0;
    border: none;
    border-radius: 4px;
    background: transparent;
}
QToolButton#heroArrow:hover {
    background-color: #2f3744;
}
QToolButton#heroArrow:pressed {
    background-color: #1f242c;
}

/* === Preset bar (top of the rack) === */
QFrame#presetBar {
    background-color: #1a1d24;
    border: 1px solid #2a2f38;
    border-radius: 6px;
}
QLabel#presetCaption {
    color: #7d8590;
    font-weight: 600;
    padding: 0 4px;
}
QToolButton#presetName {
    color: #5cc8a8;
    font-size: 11pt;
    font-weight: 700;
    min-width: 220px;
    padding: 3px 10px;
    text-align: left;
    border: 1px solid #2a2f38;
    border-radius: 5px;
    background-color: #15171c;
}
QToolButton#presetName[untitled="true"] {
    color: #9aa1ad;
    font-style: italic;
    font-weight: 500;
}
QToolButton#presetName:hover {
    border-color: #424b5c;
}
QToolButton#presetName::menu-indicator {
    subcontrol-position: right center;
    right: 6px;
}
QLabel#presetModified {
    color: #f0c850;
    font-size: 13pt;
    font-weight: 700;
    min-width: 10px;
}
QToolButton#presetTool, QToolButton#presetSlot, QToolButton#presetAb {
    color: #d8dee9;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 3px 8px;
}
QToolButton#presetTool:hover, QToolButton#presetSlot:hover, QToolButton#presetAb:hover {
    background-color: #2f3744;
}
QToolButton#presetTool::menu-indicator {
    image: none;
    width: 0;
}
QToolButton#presetSlot {
    color: #555c6b;
    min-width: 14px;
    padding: 3px 5px;
    font-weight: 600;
}
QToolButton#presetSlot[filled="true"] {
    color: #d8dee9;
    border-color: #353c4a;
}
QToolButton#presetSlot[active="true"] {
    color: #15171c;
    background-color: #5cc8a8;
    border-color: #5cc8a8;
}
QToolButton#presetAb {
    min-width: 14px;
    font-weight: 700;
    border-color: #353c4a;
}
QToolButton#presetAb:checked {
    color: #15171c;
    background-color: #5cc8a8;
    border-color: #5cc8a8;
}

/* === Tempo bar (under the preset bar) === */
QFrame#tempoBar {
    background-color: #1a1d24;
    border: 1px solid #2a2f38;
    border-radius: 6px;
}
QLineEdit#bpmEdit {
    background-color: #15171c;
    color: #5cc8a8;
    border: 1px solid #424b5c;
    border-radius: 4px;
    font-size: 12pt;
    font-weight: 700;
    selection-background-color: #5cc8a8;
    selection-color: #15171c;
}

QToolButton#metroPlay {
    color: #d8dee9;
    background-color: #2a2f38;
    border: 1px solid #353c4a;
    border-radius: 5px;
    min-width: 26px;
    padding: 3px 6px;
    font-size: 11pt;
}
QToolButton#metroPlay:hover {
    background-color: #353c4a;
}
QToolButton#metroPlay:checked {
    color: #15171c;
    background-color: #5cc8a8;
    border-color: #5cc8a8;
}

/* === Effect card ▾ detail toggle === */
QToolButton#cardMore {
    color: #9aa1ad;
    background: transparent;
    border: none;
    border-radius: 4px;
    min-width: 22px;
    font-size: 14pt;
}
QToolButton#cardMore:hover {
    color: #d8dee9;
    background-color: #2f3744;
}

/* === Tuner row: reference pitch + Hz toggle === */
QSpinBox#tunerRef {
    background-color: #15171c;
    color: #d8dee9;
    border: 1px solid #2a2f38;
    border-radius: 4px;
    padding: 3px 4px;
}
QSpinBox#tunerRef:focus {
    border-color: #424b5c;
}
QToolButton#tunerOption {
    color: #9aa1ad;
    background: transparent;
    border: 1px solid #353c4a;
    border-radius: 4px;
    padding: 3px 8px;
    font-weight: 600;
}
QToolButton#tunerOption:hover {
    background-color: #2f3744;
}
QToolButton#tunerOption:checked {
    color: #15171c;
    background-color: #5cc8a8;
    border-color: #5cc8a8;
}

/* === MIDI Mappings window === */
QDialog#midiMappings QGroupBox {
    border: 1px solid #2a2f38;
    border-radius: 6px;
    margin-top: 14px;
    padding: 8px;
}
QDialog#midiMappings QGroupBox::title {
    subcontrol-origin: margin;
    left: 8px;
    color: #9aa1ad;
}
QDialog#midiMappings QTableWidget {
    background-color: #1a1d24;
    border: 1px solid #2a2f38;
    border-radius: 5px;
    gridline-color: #2a2f38;
}
QDialog#midiMappings QHeaderView::section {
    background-color: #1f242c;
    color: #9aa1ad;
    border: none;
    border-bottom: 1px solid #2a2f38;
    padding: 4px 6px;
    font-weight: 600;
}
QDialog#midiMappings QSpinBox {
    background-color: #15171c;
    color: #d8dee9;
    border: 1px solid #2a2f38;
    border-radius: 4px;
    padding: 2px 4px;
}
QDialog#midiMappings QToolButton {
    color: #d8dee9;
    background: transparent;
    border: 1px solid #353c4a;
    border-radius: 4px;
    padding: 2px 8px;
}
QDialog#midiMappings QToolButton:hover {
    background-color: #2f3744;
}
QDialog#midiMappings QCheckBox {
    color: #d8dee9;
}

/* === Library dock === */
QDockWidget {
    color: #9aa1ad;
    font-weight: 600;
    titlebar-close-icon: none;
    titlebar-normal-icon: none;
}
QDockWidget::title {
    background-color: #1a1d24;
    border-bottom: 1px solid #2a2f38;
    padding: 5px 8px;
    text-align: left;
}
QDockWidget::close-button, QDockWidget::float-button {
    border: none;
    background: transparent;
    padding: 0;
    icon-size: 12px;
}
QDockWidget::close-button:hover, QDockWidget::float-button:hover {
    background-color: #2f3744;
    border-radius: 3px;
}
QWidget#libraryPanel {
    background-color: #15171c;
}
QToolButton#librarySection {
    color: #9aa1ad;
    background: transparent;
    border: none;
    border-bottom: 2px solid transparent;
    padding: 5px 4px;
    font-weight: 600;
}
QToolButton#librarySection:hover {
    color: #d8dee9;
}
QToolButton#librarySection:checked {
    color: #d8dee9;
    border-bottom: 2px solid #5cc8a8;
}
QToolButton#libraryMode, QToolButton#libraryTool {
    color: #9aa1ad;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 3px 8px;
}
QToolButton#libraryMode:hover, QToolButton#libraryTool:hover {
    color: #d8dee9;
    background-color: #2f3744;
}
QToolButton#libraryMode:checked {
    color: #d8dee9;
    background-color: #2a2f38;
    border-color: #353c4a;
}
QLineEdit#librarySearch {
    background-color: #1a1d24;
    color: #d8dee9;
    border: 1px solid #2a2f38;
    border-radius: 5px;
    padding: 5px 8px;
    selection-background-color: #5cc8a8;
    selection-color: #15171c;
}
QLineEdit#librarySearch:focus {
    border-color: #5cc8a8;
}
QListView#libraryList {
    background-color: #1a1d24;
    border: 1px solid #2a2f38;
    border-radius: 5px;
    outline: none;
}
QLabel#libraryStatus {
    color: #9aa1ad;
    padding-left: 4px;
}

/* === Library dock tabs (Library | TONE3000) === */
QWidget#libraryTabs {
    background-color: #1a1d24;
    border-bottom: 1px solid #2a2f38;
}
QToolButton#libraryTab {
    color: #9aa1ad;
    background: transparent;
    border: none;
    border-bottom: 2px solid transparent;
    padding: 5px 4px 6px 4px;
    font-weight: 700;
}
QToolButton#libraryTab:hover {
    color: #d8dee9;
}
QToolButton#libraryTab:checked {
    color: #d8dee9;
    border-bottom: 2px solid #5cc8a8;
}

/* === TONE3000 tab === */
QWidget#tone3000Panel {
    background-color: #15171c;
}
QLabel#t3kNotice {
    color: #f0c850;
    padding: 2px 4px;
}
QLabel#t3kSplashTitle {
    font-size: 12pt;
    font-weight: 700;
}
QLabel#t3kFinePrint {
    color: #7d8590;
    font-size: 8pt;
}
QPushButton#t3kPrimary {
    background-color: #5cc8a8;
    color: #15171c;
    border: none;
    padding: 8px 22px;
    font-weight: 700;
}
QPushButton#t3kPrimary:hover {
    background-color: #74d6b8;
}
QPushButton#t3kBrowse {
    text-align: left;
    padding: 6px 10px;
}
QToolButton#t3kUser {
    color: #d8dee9;
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 2px 6px;
}
QToolButton#t3kUser:hover {
    background-color: #2f3744;
}
QToolButton#t3kUser::menu-indicator {
    image: none;
    width: 0;
}
QLabel#t3kDetailTitle {
    font-size: 11pt;
    font-weight: 700;
}
QLabel#t3kDetailMeta {
    color: #aab0bc;
}
QLabel#t3kDetailDesc {
    color: #c3c9d4;
    padding: 2px 0;
}
QLabel#t3kSection {
    color: #9aa1ad;
    font-weight: 700;
    padding-top: 6px;
}
QLabel#heroOrigin {
    padding-left: 6px;
}

/* === Dialogs inside the window tree (Library Folders) === */
QListWidget {
    background-color: #1a1d24;
    color: #d8dee9;
    border: 1px solid #2a2f38;
    border-radius: 5px;
    outline: none;
}
QListWidget::item {
    padding: 4px 6px;
}
QListWidget::item:selected {
    background-color: #2f3744;
    color: #d8dee9;
}
QTabWidget::pane {
    border: 1px solid #2a2f38;
    border-radius: 5px;
    top: -1px;
}
QTabBar::tab {
    background: transparent;
    color: #9aa1ad;
    padding: 6px 14px;
    border-bottom: 2px solid transparent;
}
QTabBar::tab:selected {
    color: #d8dee9;
    border-bottom: 2px solid #5cc8a8;
}
QTabBar::tab:hover {
    color: #d8dee9;
}

/* === Combo boxes (library sort) === */
QComboBox {
    background-color: #2a2f38;
    color: #d8dee9;
    border: 1px solid #353c4a;
    border-radius: 5px;
    padding: 4px 8px;
    min-height: 20px;
    font-weight: 500;
}
QComboBox:hover {
    border-color: #424b5c;
}
QComboBox:disabled {
    background-color: #1d2027;
    color: #555c6b;
    border-color: #25292f;
}
QComboBox::drop-down {
    border: none;
    width: 20px;
}
QComboBox::down-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid #9aa1ad;
    margin-right: 5px;
}
QComboBox QAbstractItemView {
    background-color: #1a1d24;
    color: #d8dee9;
    border: 1px solid #353c4a;
    selection-background-color: #2f3744;
    selection-color: #d8dee9;
    outline: none;
}
QComboBox QAbstractItemView::item {
    padding: 4px 8px;
    min-height: 24px;
}
QComboBox QAbstractItemView::item:hover {
    background-color: #2f3744;
}

)";

// The only application-wide styling: tooltips are parentless top-levels, so
// they can't inherit the window stylesheet. Everything else lives in
// applyTo() — file dialogs are in-process Qt widgets even when "native" on
// KDE, and an app-wide stylesheet/palette would drag them into the dark
// theme; scoping to the window keeps them looking like the OS.
static const char* k_app_style_sheet = R"(
QToolTip {
    background-color: #232830;
    color: #d8dee9;
    border: 1px solid #353c4a;
    padding: 4px 6px;
    border-radius: 4px;
}
)";

void apply(QApplication& app) {
    app.setStyle("Fusion");
    app.setStyleSheet(QString::fromUtf8(k_app_style_sheet));
}

void apply_to(QWidget& root) {
    QPalette p;
    p.setColor(QPalette::Window, QColor("#15171c"));
    p.setColor(QPalette::WindowText, QColor("#d8dee9"));
    p.setColor(QPalette::Base, QColor("#1a1d24"));
    p.setColor(QPalette::AlternateBase, QColor("#1f242c"));
    p.setColor(QPalette::ToolTipBase, QColor("#232830"));
    p.setColor(QPalette::ToolTipText, QColor("#d8dee9"));
    p.setColor(QPalette::Text, QColor("#d8dee9"));
    p.setColor(QPalette::Button, QColor("#2a2f38"));
    p.setColor(QPalette::ButtonText, QColor("#d8dee9"));
    p.setColor(QPalette::BrightText, QColor("#ffffff"));
    p.setColor(QPalette::Mid, QColor("#353c4a"));
    p.setColor(QPalette::Dark, QColor("#15171c"));
    p.setColor(QPalette::Shadow, QColor("#0a0c10"));
    p.setColor(QPalette::Highlight, QColor("#5cc8a8"));
    p.setColor(QPalette::HighlightedText, QColor("#15171c"));

    p.setColor(QPalette::Disabled, QPalette::Text, QColor("#555c6b"));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#555c6b"));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#555c6b"));

    root.setPalette(p);

    // Per-effect accent rules — the palette hex verbatim for the name label
    // and the loaded-file label on the Amplifier/Cabinet rows (knob rings and
    // toggles get theirs pushed by tagCard).
    QString qss = QString::fromUtf8(k_style_sheet);
    for (const auto& fc : FX_COLORS) {
        qss += QString::fromUtf8("QFrame[fx=\"%1\"] QLabel#fxName { color: %2; }\n"
                                 "QFrame[fx=\"%1\"] QLabel#fileStatusOk { color: %2; }\n")
                   .arg(QLatin1String(fc.id), QLatin1String(fc.accent));
    }
    root.setStyleSheet(qss);
}

} // namespace Theme

} // namespace npl
