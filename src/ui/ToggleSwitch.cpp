#include "ToggleSwitch.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>

#include "Common.h"
#include "Theme.h"

namespace namx
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

    // Track: flat — the accent when on, the neutral track (lighter on hover) when off.
    const QColor track_off = hovered ? Theme::track_hover() : Theme::track();
    p.setBrush(isChecked() ? (hovered ? accent.lighter(110) : accent) : track_off);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(r, rx, rx);

    // Thumb: a flat disc, no shadow or highlight.
    const qreal margin = 2.0;
    const qreal thumb_d = h - 2 * margin;
    const qreal thumb_x = isChecked() ? w - margin - thumb_d : margin;
    const QRectF thumb_rect(thumb_x, margin, thumb_d, thumb_d);
    p.setBrush(Theme::thumb());
    p.setPen(Qt::NoPen);
    const QColor outline = Theme::thumb_outline();
    if (outline.isValid()) {
        p.setPen(QPen(outline, 1.0)); // a white thumb on a light track needs an edge
    }
    p.drawEllipse(outline.isValid() ? thumb_rect.adjusted(0.5, 0.5, -0.5, -0.5) : thumb_rect);
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

} // namespace namx
