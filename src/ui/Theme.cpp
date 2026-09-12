#include "Theme.h"
#include "KnobWidget.h"
#include "ToggleSwitch.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QWidget>

namespace Theme {

// ---------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------

// Per-effect accents (user-chosen, 2026-08-10). IDs match
// AudioEngine::mEffectTable; "neutral" covers Input/Output and fallbacks.
struct FxColor { const char* id; const char* accent; };
static constexpr FxColor kFxColors[] = {
    {"tuner",   "#b6aa8d"},
    {"gate",    "#8f969e"},
    {"comp",    "#4f78bd"},
    {"wah",     "#d4c19a"},
    {"flanger", "#93477b"},
    {"phaser",  "#4bba5b"},
    {"nam",     "#e4b954"},
    {"eq",      "#a5a38f"},
    {"chorus",  "#8dccf3"},
    {"tremolo", "#19848e"},
    {"delay",   "#aaa6a0"},
    {"reverb",  "#8f7d99"},
    {"ir",      "#ef661a"},
    {"neutral", "#7d8590"},
};

QColor accent(const QString& fxId)
{
    for (const auto& fc : kFxColors)
        if (fxId == QLatin1String(fc.id))
            return QColor(fc.accent);
    return QColor("#7d8590");
}

QColor genericAccent() { return QColor("#5cc8a8"); }

// Neutral chrome tones (keep in sync with the stylesheet below).
QColor track()       { return QColor("#2a2f38"); }
QColor trackHover()  { return QColor("#353c4a"); }
QColor knobBodyTop() { return QColor("#2e3441"); }
QColor knobBodyBot() { return QColor("#1c2028"); }
QColor bezel()       { return QColor("#0e1015"); }
QColor well()        { return QColor("#181b22"); }
QColor dimText()     { return QColor("#aab0bc"); }
QColor thumbTop()    { return QColor("#ffffff"); }
QColor thumbBot()    { return QColor("#d8dee9"); }

QColor meterLow()    { return QColor("#5cc8a8"); }
QColor meterMid()    { return QColor("#f0c850"); }
QColor meterHigh()   { return QColor("#e85a4f"); }
QColor meterClip()   { return QColor("#ff5252"); }

QColor ledGreen()    { return QColor("#42e86c"); }
QColor ledRed()      { return QColor("#ff453a"); }
QColor displayBg()   { return QColor("#07080c"); }

void tagCard(QWidget* card, const QString& fxId)
{
    if (!card) return;
    card->setProperty("fx", fxId);

    // The app stylesheet disables parent→child palette propagation, so the
    // accents are pushed to the interactive descendants explicitly. Call
    // tagCard AFTER the card's knobs/toggles have been created.
    const QColor a = accent(fxId);
    for (auto* k : card->findChildren<KnobWidget*>())
        k->setAccent(a);
    for (auto* t : card->findChildren<ToggleSwitch*>())
        t->setAccent(a);

    if (card->style()) {
        card->style()->unpolish(card);
        card->style()->polish(card);
    }
}

// ---------------------------------------------------------------------------
// Stylesheet
// ---------------------------------------------------------------------------

static const char* kStyleSheet = R"(
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
/* Keyboard-cursor row (RackNavigator): a faint lift, the knob ring carries
   the strong signal. */
QFrame[fxCard="true"][navRow="true"] {
    background-color: #262c36;
    border-color: #4a5566;
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

/* === Combo boxes (device selectors) === */
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
static const char* kAppStyleSheet = R"(
QToolTip {
    background-color: #232830;
    color: #d8dee9;
    border: 1px solid #353c4a;
    padding: 4px 6px;
    border-radius: 4px;
}
)";

void apply(QApplication& app)
{
    app.setStyle("Fusion");
    app.setStyleSheet(QString::fromUtf8(kAppStyleSheet));
}

void applyTo(QWidget& root)
{
    QPalette p;
    p.setColor(QPalette::Window,          QColor("#15171c"));
    p.setColor(QPalette::WindowText,      QColor("#d8dee9"));
    p.setColor(QPalette::Base,            QColor("#1a1d24"));
    p.setColor(QPalette::AlternateBase,   QColor("#1f242c"));
    p.setColor(QPalette::ToolTipBase,     QColor("#232830"));
    p.setColor(QPalette::ToolTipText,     QColor("#d8dee9"));
    p.setColor(QPalette::Text,            QColor("#d8dee9"));
    p.setColor(QPalette::Button,          QColor("#2a2f38"));
    p.setColor(QPalette::ButtonText,      QColor("#d8dee9"));
    p.setColor(QPalette::BrightText,      QColor("#ffffff"));
    p.setColor(QPalette::Mid,             QColor("#353c4a"));
    p.setColor(QPalette::Dark,            QColor("#15171c"));
    p.setColor(QPalette::Shadow,          QColor("#0a0c10"));
    p.setColor(QPalette::Highlight,       QColor("#5cc8a8"));
    p.setColor(QPalette::HighlightedText, QColor("#15171c"));

    p.setColor(QPalette::Disabled, QPalette::Text,       QColor("#555c6b"));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#555c6b"));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#555c6b"));

    root.setPalette(p);

    // Per-effect accent rules — the palette hex verbatim for the name label
    // and the loaded-file label on the Amplifier/Cabinet rows (knob rings and
    // toggles get theirs pushed by tagCard).
    QString qss = QString::fromUtf8(kStyleSheet);
    for (const auto& fc : kFxColors)
    {
        qss += QString::fromUtf8(
                   "QFrame[fx=\"%1\"] QLabel#fxName { color: %2; }\n"
                   "QFrame[fx=\"%1\"] QLabel#fileStatusOk { color: %2; }\n")
                   .arg(QLatin1String(fc.id), QLatin1String(fc.accent));
    }
    root.setStyleSheet(qss);
}

} // namespace Theme
