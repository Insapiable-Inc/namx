#include "NodeIcons.h"

#include <cmath>
#include <numbers>

#include <QPainter>
#include <QPainterPath>

namespace namx
{

namespace
{

// A sine-ish wave across `r`: `cycles` periods, amplitude `amp` (fraction of height).
QPainterPath wave(const QRectF& r, double cycles, double amp, double decay = 0.0) {
    QPainterPath path;
    constexpr int STEPS = 48;
    for (int i = 0; i <= STEPS; ++i) {
        const double t = static_cast<double>(i) / STEPS;
        const double env = std::exp(-decay * t);
        const double y = r.center().y() - std::sin(t * cycles * 2.0 * std::numbers::pi) * amp *
                                              r.height() / 2.0 * env;
        const QPointF pt(r.left() + t * r.width(), y);
        if (i == 0) {
            path.moveTo(pt);
        } else {
            path.lineTo(pt);
        }
    }
    return path;
}

} // namespace

void paint_node_icon(QPainter& p, const QRectF& box, const QString& type_id,
                     const QString& category, const QColor& color) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, std::max(1.5, box.width() / 18.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const QRectF r = box.adjusted(box.width() * 0.2, box.height() * 0.2, -box.width() * 0.2,
                                  -box.height() * 0.2);
    const double w = r.width();
    const double h = r.height();

    if (type_id == "merge" || category == "Utility") {
        // Mixer faders: three slots with knobs at different heights.
        const double xs[] = {0.2, 0.5, 0.8};
        const double ks[] = {0.35, 0.65, 0.45};
        for (int i = 0; i < 3; ++i) {
            const double x = r.left() + xs[i] * w;
            p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
            p.setBrush(color);
            p.drawRoundedRect(
                QRectF(x - w * 0.09, r.top() + ks[i] * h - h * 0.07, w * 0.18, h * 0.14), 1.5, 1.5);
            p.setBrush(Qt::NoBrush);
        }
    } else if (category == "IO") {
        // Level bars.
        for (int i = 0; i < 4; ++i) {
            const double bh = h * (0.3 + 0.23 * i);
            p.setBrush(color);
            p.drawRect(QRectF(r.left() + i * w * 0.27, r.bottom() - bh, w * 0.14, bh));
        }
    } else if (category == "Amp") {
        // Amp head: a box with a grille slot and a row of knobs.
        p.drawRoundedRect(QRectF(r.left(), r.top() + h * 0.15, w, h * 0.7), 3, 3);
        p.drawLine(QPointF(r.left() + w * 0.15, r.center().y() + h * 0.05),
                   QPointF(r.right() - w * 0.15, r.center().y() + h * 0.05));
        p.setBrush(color);
        for (int i = 0; i < 3; ++i) {
            p.drawEllipse(QPointF(r.left() + w * (0.3 + 0.2 * i), r.top() + h * 0.33), w * 0.04,
                          w * 0.04);
        }
    } else if (category == "Cab") {
        // Speaker cone.
        p.drawEllipse(r.center(), w * 0.48, h * 0.48);
        p.drawEllipse(r.center(), w * 0.26, h * 0.26);
        p.setBrush(color);
        p.drawEllipse(r.center(), w * 0.07, h * 0.07);
    } else if (category == "Drive") {
        // Clipped wave.
        QPainterPath path;
        path.moveTo(r.left(), r.center().y());
        path.lineTo(r.left() + w * 0.15, r.top() + h * 0.2);
        path.lineTo(r.left() + w * 0.4, r.top() + h * 0.2);
        path.lineTo(r.left() + w * 0.6, r.bottom() - h * 0.2);
        path.lineTo(r.left() + w * 0.85, r.bottom() - h * 0.2);
        path.lineTo(r.right(), r.center().y());
        p.drawPath(path);
    } else if (category == "Dynamics") {
        // Gain curve with a knee.
        QPainterPath path;
        path.moveTo(r.left(), r.bottom());
        path.lineTo(r.left() + w * 0.55, r.top() + h * 0.4);
        path.lineTo(r.right(), r.top() + h * 0.2);
        p.drawPath(path);
        p.setPen(QPen(color, pen.widthF() / 2, Qt::DotLine));
        p.drawLine(r.bottomLeft(), r.topRight());
    } else if (category == "Filter") {
        // A resonant low-pass / EQ curve.
        QPainterPath path;
        path.moveTo(r.left(), r.center().y());
        path.cubicTo(QPointF(r.left() + w * 0.45, r.center().y()),
                     QPointF(r.left() + w * 0.5, r.top()),
                     QPointF(r.left() + w * 0.62, r.top() + h * 0.15));
        path.cubicTo(QPointF(r.left() + w * 0.75, r.top() + h * 0.35),
                     QPointF(r.left() + w * 0.8, r.bottom()), r.bottomRight());
        p.drawPath(path);
    } else if (category == "Pitch") {
        // A burst of shifted waveform.
        p.drawPath(wave(r, 3.5, 0.9));
    } else if (category == "Modulation") {
        p.drawPath(wave(r, 1.0, 0.7));
    } else if (category == "Delay") {
        // Repeats fading out.
        for (int i = 0; i < 4; ++i) {
            const double rad = w * (0.13 - 0.025 * i);
            QColor c = color;
            c.setAlphaF(static_cast<float>(1.0 - 0.22 * i));
            p.setBrush(c);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(r.left() + w * (0.12 + 0.26 * i), r.center().y()), rad, rad);
        }
    } else if (category == "Reverb") {
        // A decaying tail.
        p.drawPath(wave(r, 4.0, 0.9, 3.0));
    } else {
        p.drawEllipse(r.center(), w * 0.4, h * 0.4);
    }
    p.restore();
}

void paint_power_icon(QPainter& p, const QRectF& r, const QColor& color) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const double side = std::min(r.width(), r.height());
    const QPointF c = r.center();
    const double rad = side * 0.36;
    p.setPen(QPen(color, std::max(1.4, side / 10.0), Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    // Ring open at the top (60° gap), bar from the centre up through the gap.
    p.drawArc(QRectF(c.x() - rad, c.y() - rad, 2 * rad, 2 * rad), 120 * 16, 300 * 16);
    p.drawLine(QPointF(c.x(), c.y() - rad * 1.15), QPointF(c.x(), c.y() - rad * 0.15));
    p.restore();
}

} // namespace namx
