#include "RackNavigator.h"
#include "KnobWidget.h"
#include "ToggleSwitch.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScrollArea>
#include <QStyle>
#include <QTextEdit>
#include <QWidget>

#include <algorithm>

namespace {

// Dynamic property read by the Theme stylesheet to lift the focused row.
const char kRowProp[] = "navRow";

void setRowProperty(QWidget* card, bool on)
{
    if (!card) return;
    card->setProperty(kRowProp, on);
    card->style()->unpolish(card);
    card->style()->polish(card);
    card->update();
}

} // namespace

RackNavigator::RackNavigator(QWidget* window, QScrollArea* strip, QObject* parent)
    : QObject(parent)
    , mWindow(window)
    , mStrip(strip)
{
    qApp->installEventFilter(this);
}

void RackNavigator::setRows(std::vector<Row> rows)
{
    QWidget* keep = (mRow >= 0 && mRow < static_cast<int>(mRows.size())) ? mRows[mRow].card : nullptr;
    const bool wasVisible = mVisible;
    if (wasVisible) applyHighlight(mRow, mKnob, false);

    mRows = std::move(rows);

    int row = 0;
    for (int i = 0; i < static_cast<int>(mRows.size()); ++i)
        if (mRows[i].card == keep) { row = i; break; }

    mRow  = row;
    mKnob = currentRow() && !currentRow()->knobs.empty()
              ? std::min(mPreferredKnob, static_cast<int>(currentRow()->knobs.size()) - 1)
              : 0;
    if (wasVisible) applyHighlight(mRow, mKnob, true);
}

RackNavigator::Row* RackNavigator::currentRow()
{
    if (mRow < 0 || mRow >= static_cast<int>(mRows.size())) return nullptr;
    return &mRows[mRow];
}

KnobWidget* RackNavigator::currentKnob() const
{
    if (mRow < 0 || mRow >= static_cast<int>(mRows.size())) return nullptr;
    const auto& knobs = mRows[mRow].knobs;
    if (mKnob < 0 || mKnob >= static_cast<int>(knobs.size())) return nullptr;
    return knobs[mKnob];
}

void RackNavigator::applyHighlight(int row, int knob, bool on)
{
    if (row < 0 || row >= static_cast<int>(mRows.size())) return;
    const Row& r = mRows[row];
    setRowProperty(r.card, on);
    if (knob >= 0 && knob < static_cast<int>(r.knobs.size()))
        r.knobs[knob]->setNavHighlight(on);
}

void RackNavigator::setCursor(int row, int knob)
{
    if (mRows.empty()) return;
    row = std::clamp(row, 0, static_cast<int>(mRows.size()) - 1);
    const int nKnobs = static_cast<int>(mRows[row].knobs.size());
    knob = nKnobs > 0 ? std::clamp(knob, 0, nKnobs - 1) : 0;

    if (mVisible) applyHighlight(mRow, mKnob, false);
    mRow  = row;
    mKnob = knob;
    if (mVisible)
    {
        applyHighlight(mRow, mKnob, true);
        ensureVisible();
    }
}

void RackNavigator::show()
{
    if (mVisible || mRows.empty()) return;
    mVisible = true;
    applyHighlight(mRow, mKnob, true);
    ensureVisible();
}

void RackNavigator::hide()
{
    if (!mVisible) return;
    applyHighlight(mRow, mKnob, false);
    mVisible = false;
}

void RackNavigator::ensureVisible()
{
    Row* r = currentRow();
    if (!r || !r->card || !mStrip) return;
    // Only strip cards live inside the scroll area; Input/Output flank it.
    if (mStrip->widget() && mStrip->widget()->isAncestorOf(r->card))
        mStrip->ensureWidgetVisible(r->card, 0, 8);
}

void RackNavigator::moveRow(int delta)
{
    if (mRows.empty()) return;
    const int n   = static_cast<int>(mRows.size());
    const int row = ((mRow + delta) % n + n) % n; // wrap both ways
    setCursor(row, mPreferredKnob);
}

void RackNavigator::moveKnob(int delta)
{
    Row* r = currentRow();
    if (!r || r->knobs.empty()) return;
    const int knob = std::clamp(mKnob + delta, 0, static_cast<int>(r->knobs.size()) - 1);
    mPreferredKnob = knob;
    setCursor(mRow, knob);
}

// The cursor keys are global within the main window, but must never steal
// keystrokes from a text field, an open menu, or a dialog.
bool RackNavigator::keysApplyNow() const
{
    if (QApplication::activePopupWidget()) return false;
    if (QApplication::activeModalWidget()) return false;
    if (QApplication::activeWindow() != mWindow) return false;

    QWidget* f = QApplication::focusWidget();
    if (!f) return true;
    if (qobject_cast<QLineEdit*>(f) || qobject_cast<QTextEdit*>(f)
        || qobject_cast<QAbstractSpinBox*>(f) || qobject_cast<QComboBox*>(f))
        return false;
    return true;
}

bool RackNavigator::eventFilter(QObject* obj, QEvent* ev)
{
    switch (ev->type())
    {
    case QEvent::KeyPress:
    {
        auto* w = qobject_cast<QWidget*>(obj);
        if (!w || w->window() != mWindow) break;
        if (!keysApplyNow()) break;
        if (handleKey(static_cast<QKeyEvent*>(ev)))
            return true;
        break;
    }
    case QEvent::MouseButtonPress:
    {
        // A click on a knob moves the cursor there without revealing it, so
        // a later key press continues from where the mouse left off.
        auto* w = qobject_cast<QWidget*>(obj);
        if (!w || w->window() != mWindow || mRows.empty()) break;
        for (int r = 0; r < static_cast<int>(mRows.size()); ++r)
        {
            const auto& knobs = mRows[r].knobs;
            for (int k = 0; k < static_cast<int>(knobs.size()); ++k)
            {
                if (knobs[k] == w || knobs[k]->isAncestorOf(w))
                {
                    mPreferredKnob = k;
                    setCursor(r, k);
                    return false;
                }
            }
        }
        break;
    }
    default:
        break;
    }
    return QObject::eventFilter(obj, ev);
}

bool RackNavigator::handleKey(QKeyEvent* ke)
{
    if (mRows.empty()) return false;

    const auto mods = ke->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const int  key  = ke->key();

    // Value keys. On a Latin American layout Shift++ types '*' and Shift+-
    // types '_', so those count as the shifted (fine) variants.
    int direction = 0;
    if (key == Qt::Key_Plus  || key == Qt::Key_Asterisk)   direction = +1;
    if (key == Qt::Key_Minus || key == Qt::Key_Underscore) direction = -1;

    const bool navKey = key == Qt::Key_Up || key == Qt::Key_Down
                     || key == Qt::Key_Left || key == Qt::Key_Right;
    const bool plainMods = mods == Qt::NoModifier;

    if (navKey && plainMods)
    {
        if (!mVisible) { show(); return true; } // first press reveals, doesn't move
        switch (key)
        {
        case Qt::Key_Up:    moveRow(-1);  break;
        case Qt::Key_Down:  moveRow(+1);  break;
        case Qt::Key_Left:  moveKnob(-1); break;
        case Qt::Key_Right: moveKnob(+1); break;
        }
        return true;
    }

    if (direction != 0 && !(mods & (Qt::AltModifier | Qt::MetaModifier)))
    {
        KnobWidget* knob = currentKnob();
        if (!knob) return false;
        show();
        double scale = 1.0;
        if (mods & Qt::ShiftModifier)   scale *= 0.1;
        if (mods & Qt::ControlModifier) scale *= 10.0;
        knob->stepBy(direction, scale);
        return true;
    }

    if (!plainMods) return false;

    switch (key)
    {
    case Qt::Key_Escape:
        if (!mVisible) return false;
        hide();
        return true;

    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_Delete:
    {
        KnobWidget* knob = currentKnob();
        if (!knob) return false;
        show();
        if (key == Qt::Key_Home)      knob->setValue(knob->minimum());
        else if (key == Qt::Key_End)  knob->setValue(knob->maximum());
        else                          knob->resetToDefault();
        return true;
    }

    case Qt::Key_Space:
    {
        Row* r = currentRow();
        if (!r || !r->enable) return false;
        show();
        r->enable->toggle();
        return true;
    }

    case Qt::Key_S:
    {
        Row* r = currentRow();
        if (!r || !r->stereo) return false;
        show();
        r->stereo->toggle();
        return true;
    }

    case Qt::Key_Return:
    case Qt::Key_Enter:
    {
        Row* r = currentRow();
        if (!r) return false;
        if (r->activate) { show(); r->activate(); return true; }
        if (KnobWidget* knob = currentKnob()) { show(); typeValue(knob); return true; }
        return false;
    }

    default:
        return false;
    }
}

void RackNavigator::typeValue(KnobWidget* knob)
{
    bool ok = false;
    const QString title = knob->unit().isEmpty()
        ? knob->label()
        : QString("%1 (%2)").arg(knob->label(), knob->unit());
    const double v = QInputDialog::getDouble(mWindow, title, "Value:",
                                             knob->value(), knob->minimum(), knob->maximum(),
                                             2, &ok);
    if (ok) knob->setValue(v);
}
