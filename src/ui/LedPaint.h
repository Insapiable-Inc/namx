#pragma once

#include <QColor>
#include <QPainter>
#include <QRectF>

#include "Common.h"

namespace namx
{

// Painting for the LED-style displays (tuner, level meters).

// A lit LED: soft halo passes, then the solid core.
inline void draw_lit_led(QPainter& p, const QRectF& r, const QColor& c, qreal radius) {
    QColor halo = c;
    p.setPen(Qt::NoPen);
    halo.setAlpha(28);
    p.setBrush(halo);
    p.drawRoundedRect(r.adjusted(-3.0, -3.0, 3.0, 3.0), radius + 3.0, radius + 3.0);
    halo.setAlpha(70);
    p.setBrush(halo);
    p.drawRoundedRect(r.adjusted(-1.2, -1.2, 1.2, 1.2), radius + 1.2, radius + 1.2);
    p.setBrush(c);
    p.drawRoundedRect(r, radius, radius);
}

// An unlit LED: dark, barely tinted with its color.
inline void draw_unlit_led(QPainter& p, const QRectF& r, QColor c, qreal radius, int alpha = 14) {
    c.setAlpha(alpha);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(r, radius, radius);
}

} // namespace namx
