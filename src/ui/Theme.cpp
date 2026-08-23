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

/* === Group boxes (effect cards) === */
QGroupBox {
    background-color: #1f242c;
    border: 1px solid #2a2f38;
    border-top: 3px solid #7d8590;
    border-radius: 6px;
    margin-top: 16px;
    padding: 14px 4px 6px 4px;
    font-weight: 600;
    font-size: 10pt; /* card title size (QSS fonts don't propagate to children) */
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top center;
    padding: 0 8px;
    color: #d8dee9;
    background: transparent;
    margin-top: -2px;
}
QGroupBox[selected="true"] {
    background-color: #29313c;
    border-left: 2px solid #657184;
    border-right: 2px solid #657184;
    border-bottom: 2px solid #657184;
}
QStackedWidget#effectEditor {
    background: transparent;
}
QGroupBox#effectEditorPage {
    min-height: 150px;
}
QLabel#editorEmptyState {
    color: #7d8590;
    font-style: italic;
}
/* Per-effect accent stripes and title colors are appended in apply(). */

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

/* === Progress bars (VU meters, tuner bar) === */
QProgressBar {
    background-color: #0f1116;
    border: 1px solid #2a2f38;
    border-radius: 3px;
    text-align: center;
}
QProgressBar::chunk {
    border-radius: 2px;
    margin: 1px;
}
QProgressBar#vuMeter::chunk {
    background: qlineargradient(x1:0,y1:1,x2:0,y2:0,
        stop:0 #5cc8a8, stop:0.65 #f0c850, stop:0.9 #e85a4f, stop:1 #ff5252);
}
QProgressBar#tunerBar::chunk {
    background: #5cc8a8;
}

/* === Frames (chain dividers) === */
QFrame[frameShape="5"] {  /* Qt::VLine */
    color: #2a2f38;
}

/* === Labels === */
QLabel {
    background: transparent;
}
QLabel#stereoLabel {
    color: #aab0bc;
}

/* === App title in the top status row === */
QLabel#appTitle {
    color: #e85a4f;
    font-size: 18pt;
    font-weight: 700;
    padding-right: 10px;
}

/* === Audio status pill === */
QLabel#audioStatusOk {
    background-color: rgba(92, 200, 168, 0.15);
    color: #5cc8a8;
    padding: 4px 12px;
    border-radius: 11px;
    font-weight: 600;
}
QLabel#audioStatusErr {
    background-color: rgba(232, 90, 79, 0.18);
    color: #e85a4f;
    padding: 4px 12px;
    border-radius: 11px;
    font-weight: 600;
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

/* === Tuner === */
QLabel#tunerNote {
    color: #d8dee9;
    font-weight: 700;
}
QLabel#tunerCents {
    color: #9aa1ad;
}

/* === Tooltips === */
QToolTip {
    background-color: #232830;
    color: #d8dee9;
    border: 1px solid #353c4a;
    padding: 4px 6px;
    border-radius: 4px;
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

/* Device selector labels */
QLabel#deviceLabel {
    color: #9aa1ad;
    font-weight: 600;
    padding-right: 4px;
}
)";

void apply(QApplication& app)
{
    app.setStyle("Fusion");

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

    app.setPalette(p);

    // Per-effect accent rules — the palette hex verbatim for stripe and title.
    QString qss = QString::fromUtf8(kStyleSheet);
    for (const auto& fc : kFxColors)
    {
        qss += QString::fromUtf8(
                   "QGroupBox[fx=\"%1\"] { border-top: 3px solid %2; }\n"
                   "QGroupBox[fx=\"%1\"]::title { color: %2; }\n")
                   .arg(QLatin1String(fc.id), QLatin1String(fc.accent));
    }
    app.setStyleSheet(qss);
}

} // namespace Theme
