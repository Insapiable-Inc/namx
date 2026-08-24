#include "VUMeter.h"
#include "Theme.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

VUMeter::VUMeter(Qt::Orientation orientation, QWidget* parent)
    : QProgressBar(parent)
{
    setOrientation(orientation);
    setRange(0, 100);
    setValue(0);
    setTextVisible(false);
    if (orientation == Qt::Horizontal)
    {
        setFixedHeight(14);
        setMinimumWidth(70);
    }
    else
    {
        setFixedWidth(14);
        setMinimumHeight(70);
    }
}

void VUMeter::setPeak(float peak)
{
    int v = 0;
    if (peak > 0.0f)
    {
        const float dB = 20.0f * std::log10(peak);
        v = static_cast<int>(std::lround(
            std::clamp((dB - kMinDb) / (kMaxDb - kMinDb) * 100.0f, 0.0f, 100.0f)));
    }

    // Peak hold: track the recent maximum, then let it fall.
    if (static_cast<float>(v) >= mHold)
    {
        mHold    = static_cast<float>(v);
        mHoldAge = 0;
    }
    else if (++mHoldAge > kHoldTicks)
    {
        mHold = std::max(static_cast<float>(v), mHold - kHoldFall);
    }

    setValue(v);
    update(); // the hold line moves even when the level value doesn't
}

void VUMeter::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Groove.
    const QRectF groove = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(Theme::track(), 1.0));
    p.setBrush(Theme::bezel());
    p.drawRoundedRect(groove, 3, 3);

    const QRectF inner = groove.adjusted(1.5, 1.5, -1.5, -1.5);
    const bool   vert  = orientation() == Qt::Vertical;

    // Position (px offset along the meter axis from the quiet end) of a
    // 0..100 value.
    const qreal span = vert ? inner.height() : inner.width();
    auto pos = [span](float v) { return span * (v / 100.0f); };

    // Everything below is clipped to the rounded inner region so the zone
    // rectangles keep the rounded silhouette.
    QPainterPath clip;
    clip.addRoundedRect(inner, 2, 2);
    p.setClipPath(clip);
    p.setPen(Qt::NoPen);

    // Zone fill: one rect per zone the level reaches — color by absolute
    // level, red strictly above the 0 dBFS mark.
    const float t = static_cast<float>(value());
    auto zoneRect = [&](float from, float to) -> QRectF {
        const qreal a = pos(from), b = pos(to);
        return vert ? QRectF(inner.left(), inner.bottom() - b,
                             inner.width(), b - a)
                    : QRectF(inner.left() + a, inner.top(),
                             b - a, inner.height());
    };
    if (t > 0.0f)
    {
        p.setBrush(Theme::meterLow());
        p.drawRect(zoneRect(0.0f, std::min(t, kHotValue)));
    }
    if (t > kHotValue)
    {
        p.setBrush(Theme::meterMid());
        p.drawRect(zoneRect(kHotValue, std::min(t, kClipValue)));
    }
    if (t > kClipValue)
    {
        p.setBrush(Theme::meterClip());
        p.drawRect(zoneRect(kClipValue, t));
    }

    // 0 dBFS mark on the groove.
    {
        const qreal c = pos(kClipValue);
        p.setBrush(Theme::track());
        if (vert)
            p.drawRect(QRectF(inner.left(), inner.bottom() - c - 0.5,
                              inner.width(), 1.0));
        else
            p.drawRect(QRectF(inner.left() + c - 0.5, inner.top(),
                              1.0, inner.height()));
    }

    // Peak-hold line, colored by the zone it sits in.
    if (mHold > 0.5f)
    {
        p.setBrush(mHold > kClipValue ? Theme::meterClip()
                 : mHold > kHotValue  ? Theme::meterMid()
                                      : Theme::meterLow());
        const qreal hp = pos(mHold);
        if (vert)
            p.drawRect(QRectF(inner.left(), inner.bottom() - hp - 1.0,
                              inner.width(), 2.0));
        else
            p.drawRect(QRectF(inner.left() + hp - 1.0, inner.top(),
                              2.0, inner.height()));
    }
}
