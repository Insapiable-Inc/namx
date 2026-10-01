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
    if (!mClock.isValid()) mClock.start();
    const qint64 now = mClock.elapsed();
    const float  dt  = static_cast<float>(now - mLastMs) / 1000.0f;
    mLastMs = now;

    const float inDb = peak > 0.0f ? std::clamp(20.0f * std::log10(peak), kMinDb, kMaxDb)
                                   : kMinDb;

    // Instant attack, linear-in-dB release.
    mLevelDb = inDb >= mLevelDb ? inDb : std::max(inDb, mLevelDb - kReleaseDbPerSec * dt);

    // Peak hold: track the recent maximum, wait, then fall (never below the bar).
    if (inDb >= mHoldDb)
    {
        mHoldDb      = inDb;
        mHoldSinceMs = now;
    }
    else if (now - mHoldSinceMs > kHoldMs)
    {
        mHoldDb = std::max(mLevelDb, mHoldDb - kHoldFallDbPerSec * dt);
    }

    // Repaint only on visible movement — a silent input costs nothing.
    const float level = toValue(mLevelDb);
    const float hold  = toValue(mHoldDb);
    if (std::abs(level - mLevel) < 0.05f && std::abs(hold - mHold) < 0.05f) return;
    mLevel = level;
    mHold  = hold;
    update();
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
    const float t = mLevel;
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
