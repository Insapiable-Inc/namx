#include "ToggleSwitch.h"

#include <QPainter>
#include <QMouseEvent>
#include <QApplication>
#include <QPalette>

ToggleSwitch::ToggleSwitch(QWidget* parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void ToggleSwitch::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRect  r  = rect();
    const int    h  = r.height();
    const int    w  = r.width();
    const double rx = h / 2.0;

    // Track color
    QColor trackOn  = palette().color(QPalette::Highlight);
    QColor trackOff = palette().color(QPalette::Mid);
    if (mHovered && !isChecked())
        trackOff = trackOff.lighter(130);

    QColor trackColor = isChecked() ? trackOn : trackOff;

    // Track
    p.setPen(Qt::NoPen);
    p.setBrush(trackColor);
    p.drawRoundedRect(r, rx, rx);

    // Thumb
    const int margin  = 2;
    const int thumbD  = h - 2 * margin;
    const int thumbX  = isChecked() ? w - margin - thumbD : margin;

    QColor thumbColor = palette().color(QPalette::BrightText);
    if (!thumbColor.isValid() || thumbColor == Qt::transparent)
        thumbColor = Qt::white;

    p.setBrush(thumbColor);
    p.setPen(Qt::NoPen);
    p.drawEllipse(thumbX, margin, thumbD, thumbD);
}

void ToggleSwitch::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && rect().contains(e->pos()))
    {
        setChecked(!isChecked());
        emit toggled(isChecked());
        emit clicked(isChecked());
    }
    e->accept();
}

void ToggleSwitch::enterEvent(QEnterEvent* e)
{
    mHovered = true;
    update();
    QAbstractButton::enterEvent(e);
}

void ToggleSwitch::leaveEvent(QEvent* e)
{
    mHovered = false;
    update();
    QAbstractButton::leaveEvent(e);
}
