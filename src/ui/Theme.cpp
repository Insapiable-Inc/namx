#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QWidget>

namespace Theme {

const char* categoryName(Category c)
{
    switch (c) {
        case Category::Neutral:  return "neutral";
        case Category::Dynamics: return "dynamics";
        case Category::Filter:   return "filter";
        case Category::ModPre:   return "modpre";
        case Category::Amp:      return "amp";
        case Category::EQ:       return "eq";
        case Category::ModPost:  return "modpost";
        case Category::Time:     return "time";
        case Category::Cab:      return "cab";
    }
    return "neutral";
}

QColor categoryColor(Category c)
{
    switch (c) {
        case Category::Neutral:  return QColor("#7d8590");
        case Category::Dynamics: return QColor("#5cc8a8");
        case Category::Filter:   return QColor("#b07be8");
        case Category::ModPre:   return QColor("#f0a040");
        case Category::Amp:      return QColor("#e85a4f");
        case Category::EQ:       return QColor("#5e8eda");
        case Category::ModPost:  return QColor("#f0c850");
        case Category::Time:     return QColor("#4a90e2");
        case Category::Cab:      return QColor("#d878a0");
    }
    return QColor("#7d8590");
}

void tagCard(QWidget* card, Category c)
{
    if (!card) return;
    card->setProperty("category", categoryName(c));

    QPalette pal = card->palette();
    pal.setColor(QPalette::Highlight, categoryColor(c));
    card->setPalette(pal);

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
    font-size: 9pt;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top center;
    padding: 0 8px;
    color: #d8dee9;
    background: transparent;
    margin-top: -2px;
}

/* Category accents — top edge of each card */
QGroupBox[category="neutral"]  { border-top: 3px solid #7d8590; }
QGroupBox[category="dynamics"] { border-top: 3px solid #5cc8a8; }
QGroupBox[category="filter"]   { border-top: 3px solid #b07be8; }
QGroupBox[category="modpre"]   { border-top: 3px solid #f0a040; }
QGroupBox[category="amp"]      { border-top: 3px solid #e85a4f; }
QGroupBox[category="eq"]       { border-top: 3px solid #5e8eda; }
QGroupBox[category="modpost"]  { border-top: 3px solid #f0c850; }
QGroupBox[category="time"]     { border-top: 3px solid #4a90e2; }
QGroupBox[category="cab"]      { border-top: 3px solid #d878a0; }

/* Title color reflects the accent */
QGroupBox[category="dynamics"]::title { color: #88dcc0; }
QGroupBox[category="filter"]::title   { color: #c79bf0; }
QGroupBox[category="modpre"]::title   { color: #f5bb6b; }
QGroupBox[category="amp"]::title      { color: #f08272; }
QGroupBox[category="eq"]::title       { color: #88aae6; }
QGroupBox[category="modpost"]::title  { color: #f5d878; }
QGroupBox[category="time"]::title     { color: #78a9eb; }
QGroupBox[category="cab"]::title      { color: #e598b8; }

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

/* === Progress bars (VU meters, tuner bar) ===
   Per-widget chunk gradients are set in code (see MainWindow). */
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

/* === Frames (chain dividers) === */
QFrame[frameShape="5"] {  /* Qt::VLine */
    color: #2a2f38;
}

/* === Labels === */
QLabel {
    background: transparent;
}

/* === App title in the top status row === */
QLabel#appTitle {
    color: #e85a4f;
    font-size: 18pt;
    font-weight: 700;
    padding-right: 10px;
}

/* === JACK status pill === */
QLabel#jackStatusOk {
    background-color: rgba(92, 200, 168, 0.15);
    color: #5cc8a8;
    padding: 4px 12px;
    border-radius: 11px;
    font-weight: 600;
}
QLabel#jackStatusErr {
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
    app.setStyleSheet(QString::fromUtf8(kStyleSheet));
}

} // namespace Theme
