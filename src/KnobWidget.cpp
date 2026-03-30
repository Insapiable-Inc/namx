#include "KnobWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFont>
#include <cmath>

KnobWidget::KnobWidget(const QString& label, double minVal, double maxVal,
                       double defaultVal, const QString& unit, QWidget* parent)
    : QWidget(parent)
    , mValue(defaultVal)
    , mMin(minVal)
    , mMax(maxVal)
    , mDefault(defaultVal)
    , mLabel(label)
    , mUnit(unit)
{
    setMouseTracking(false);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void KnobWidget::setValue(double v)
{
    double clamped = clamp(v);
    if (clamped == mValue)
        return;
    mValue = clamped;
    update();
    emit valueChanged(mValue);
}

void KnobWidget::resetToDefault()
{
    setValue(mDefault);
}

void KnobWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRect r = rect();
    const int   labelH  = 18;
    const int   valueH  = 16;
    const int   knobDia = std::min(r.width(), r.height() - labelH - valueH) - 8;
    const int   knobX   = (r.width() - knobDia) / 2;
    const int   knobY   = 4;

    // ---- Background ----
    p.fillRect(r, QColor(30, 30, 35));

    // ---- Track arc (background) ----
    const QRectF knobRect(knobX, knobY, knobDia, knobDia);
    const QRectF arcRect = knobRect.adjusted(4, 4, -4, -4);

    QPen trackPen(QColor(60, 60, 70), 4, Qt::SolidLine, Qt::FlatCap);
    p.setPen(trackPen);
    // Qt angle: 0=3 o'clock, CCW positive. We want start at 225 deg from 3'oclock going CW.
    // Qt uses 16ths of a degree, and drawArc goes CCW.
    // Start angle from 3 o'clock: 225 degrees clockwise = -(225)*16
    const int startAngle = (90 + kArcSpanDeg / 2) * 16; // start at bottom-left
    const int spanAngle  = -kArcSpanDeg * 16;
    p.drawArc(arcRect, startAngle, spanAngle);

    // ---- Value arc (filled portion) ----
    const double norm     = normalised();
    const int    fillSpan = static_cast<int>(-norm * kArcSpanDeg * 16);
    QPen valuePen(QColor(220, 120, 40), 4, Qt::SolidLine, Qt::FlatCap);
    p.setPen(valuePen);
    p.drawArc(arcRect, startAngle, fillSpan);

    // ---- Knob body ----
    QRadialGradient grad(knobRect.center(), knobDia / 2.0);
    grad.setColorAt(0.0, QColor(85, 85, 95));
    grad.setColorAt(1.0, QColor(45, 45, 52));
    p.setPen(QPen(QColor(20, 20, 25), 1));
    p.setBrush(grad);
    p.drawEllipse(knobRect.adjusted(6, 6, -6, -6));

    // ---- Pointer line ----
    const double angleDeg  = 225.0 - norm * kArcSpanDeg; // degrees CW from 12 o'clock
    const double angleRad  = (angleDeg - 90.0) * M_PI / 180.0;
    const QPointF center   = knobRect.adjusted(6, 6, -6, -6).center();
    const double radius    = (knobDia / 2.0 - 8);
    QPointF tipPt(center.x() + radius * 0.65 * std::cos(angleRad),
                  center.y() + radius * 0.65 * std::sin(angleRad));
    p.setPen(QPen(QColor(240, 200, 140), 2, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(center, tipPt);

    // ---- Label ----
    QFont lf("Sans", 8, QFont::Medium);
    p.setFont(lf);
    p.setPen(QColor(160, 160, 170));
    QRect labelRect(0, knobY + knobDia + 2, r.width(), labelH);
    p.drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop, mLabel);

    // ---- Value text ----
    QFont vf("Monospace", 7);
    p.setFont(vf);
    p.setPen(QColor(220, 120, 40));
    QString valStr = QString::number(mValue, 'f', 1) + " " + mUnit;
    QRect valueRect(0, knobY + knobDia + labelH, r.width(), valueH);
    p.drawText(valueRect, Qt::AlignHCenter | Qt::AlignTop, valStr);
}

void KnobWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        mDragging      = true;
        mDragStartY    = event->pos().y();
        mDragStartValue = mValue;
    }
}

void KnobWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!mDragging)
        return;
    const int   dy    = mDragStartY - event->pos().y();
    const double range = mMax - mMin;
    // 200 pixels = full range
    double newVal = mDragStartValue + dy * (range / 200.0);
    setValue(newVal);
}

void KnobWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        mDragging = false;
}

void KnobWidget::mouseDoubleClickEvent(QMouseEvent*)
{
    resetToDefault();
}

void KnobWidget::wheelEvent(QWheelEvent* event)
{
    const double step = (mMax - mMin) / 100.0;
    const int    delta = event->angleDelta().y();
    setValue(mValue + (delta > 0 ? step : -step));
    event->accept();
}
