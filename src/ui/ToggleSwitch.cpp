#include "ToggleSwitch.h"

#include <QApplication>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QRadialGradient>

#include "Common.h"
#include "Theme.h"

namespace npl
{

ToggleSwitch::ToggleSwitch(QWidget* parent)
    : QAbstractButton(parent)
    , accent(Theme::generic_accent()) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void ToggleSwitch::set_accent(const QColor& c) {
    accent = c;
    update();
}

void ToggleSwitch::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r = rect();
    const qreal h = r.height();
    const qreal w = r.width();
    const qreal rx = h / 2.0;

    // Track
    const QColor color = accent;
    const QColor track_off = hovered ? Theme::track_hover() : Theme::track();

    if (isChecked()) {
        QLinearGradient trk(0, 0, 0, h);
        trk.setColorAt(0.0, color.lighter(115));
        trk.setColorAt(1.0, color.darker(110));
        p.setBrush(trk);
    } else {
        p.setBrush(track_off);
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
        const qreal led_d = h * 0.32;
        const qreal led_x = 5.0;
        const qreal led_y = (h - led_d) / 2.0;
        QRadialGradient g(QPointF(led_x + led_d / 2.0, led_y + led_d / 2.0), led_d);
        g.setColorAt(0.0, QColor(255, 255, 255, 230));
        g.setColorAt(0.5, QColor(255, 255, 255, 80));
        g.setColorAt(1.0, QColor(255, 255, 255, 0));
        p.setBrush(g);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(led_x, led_y, led_d, led_d));
    }

    // Thumb
    const qreal margin = 2.0;
    const qreal thumb_d = h - 2 * margin;
    const qreal thumb_x = isChecked() ? w - margin - thumb_d : margin;
    const QRectF thumb_rect(thumb_x, margin, thumb_d, thumb_d);

    // Soft shadow under the thumb.
    p.setBrush(QColor(0, 0, 0, 70));
    p.setPen(Qt::NoPen);
    p.drawEllipse(thumb_rect.translated(0, 1.0));

    // Thumb body with a top highlight.
    QLinearGradient thumb_grad(thumb_rect.topLeft(), thumb_rect.bottomLeft());
    thumb_grad.setColorAt(0.0, Theme::thumb_top());
    thumb_grad.setColorAt(1.0, Theme::thumb_bot());
    p.setBrush(thumb_grad);
    p.drawEllipse(thumb_rect);
}

void ToggleSwitch::mouseReleaseEvent(QMouseEvent* e) {
    // click() toggles a checkable button and emits toggled/clicked exactly once.
    if (e->button() == Qt::LeftButton && rect().contains(e->pos())) {
        click();
    }
    e->accept();
}

void ToggleSwitch::enterEvent(QEnterEvent* e) {
    hovered = true;
    update();
    QAbstractButton::enterEvent(e);
}

void ToggleSwitch::leaveEvent(QEvent* e) {
    hovered = false;
    update();
    QAbstractButton::leaveEvent(e);
}

} // namespace npl
