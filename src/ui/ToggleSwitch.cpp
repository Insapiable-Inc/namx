#include "ToggleSwitch.h"

#include <QApplication>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QRadialGradient>

#include "Theme.h"

ToggleSwitch::ToggleSwitch(QWidget* parent)
    : QAbstractButton(parent)
    , mAccent(Theme::genericAccent()) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void ToggleSwitch::setAccent(const QColor& c) {
    mAccent = c;
    update();
}

void ToggleSwitch::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r = rect();
    const qreal h = r.height();
    const qreal w = r.width();
    const qreal rx = h / 2.0;

    // ---- Track ----
    const QColor accent = mAccent;
    const QColor trackOff = mHovered ? Theme::trackHover() : Theme::track();

    if (isChecked()) {
        QLinearGradient trk(0, 0, 0, h);
        trk.setColorAt(0.0, accent.lighter(115));
        trk.setColorAt(1.0, accent.darker(110));
        p.setBrush(trk);
    } else {
        p.setBrush(trackOff);
    }
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(r, rx, rx);

    // Subtle inset shadow line on the top edge.
    QPen rim(QColor(0, 0, 0, isChecked() ? 60 : 100), 1.0);
    p.setPen(rim);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), rx - 0.5, rx - 0.5);

    // LED glow when on: small bright dot inside the off-side of the track.
    if (isChecked()) {
        const qreal ledD = h * 0.32;
        const qreal ledX = 5.0;
        const qreal ledY = (h - ledD) / 2.0;
        QRadialGradient g(QPointF(ledX + ledD / 2.0, ledY + ledD / 2.0), ledD);
        g.setColorAt(0.0, QColor(255, 255, 255, 230));
        g.setColorAt(0.5, QColor(255, 255, 255, 80));
        g.setColorAt(1.0, QColor(255, 255, 255, 0));
        p.setBrush(g);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(ledX, ledY, ledD, ledD));
    }

    // ---- Thumb ----
    const qreal margin = 2.0;
    const qreal thumbD = h - 2 * margin;
    const qreal thumbX = isChecked() ? w - margin - thumbD : margin;
    const QRectF thumbRect(thumbX, margin, thumbD, thumbD);

    // Soft shadow under the thumb.
    p.setBrush(QColor(0, 0, 0, 70));
    p.setPen(Qt::NoPen);
    p.drawEllipse(thumbRect.translated(0, 1.0));

    // Thumb body with a top highlight.
    QLinearGradient thumbGrad(thumbRect.topLeft(), thumbRect.bottomLeft());
    thumbGrad.setColorAt(0.0, Theme::thumbTop());
    thumbGrad.setColorAt(1.0, Theme::thumbBot());
    p.setBrush(thumbGrad);
    p.drawEllipse(thumbRect);
}

void ToggleSwitch::mouseReleaseEvent(QMouseEvent* e) {
    // click() toggles a checkable button and emits toggled/clicked exactly once.
    if (e->button() == Qt::LeftButton && rect().contains(e->pos())) {
        click();
    }
    e->accept();
}

void ToggleSwitch::enterEvent(QEnterEvent* e) {
    mHovered = true;
    update();
    QAbstractButton::enterEvent(e);
}

void ToggleSwitch::leaveEvent(QEvent* e) {
    mHovered = false;
    update();
    QAbstractButton::leaveEvent(e);
}
