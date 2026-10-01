#include "VUMeter.h"

#include <algorithm>
#include <cmath>

#include <QPainter>
#include <QPainterPath>

#include "Theme.h"

VUMeter::VUMeter(Qt::Orientation orientation, QWidget* parent)
    : QProgressBar(parent) {
    setOrientation(orientation);
    setRange(0, 100);
    setValue(0);
    setTextVisible(false);
    if (orientation == Qt::Horizontal) {
        setFixedHeight(14);
        setMinimumWidth(70);
    } else {
        setFixedWidth(14);
        setMinimumHeight(70);
    }
}

void VUMeter::set_peak(float peak) {
    if (!m_clock.isValid()) {
        m_clock.start();
    }
    const qint64 now = m_clock.elapsed();
    const float dt = static_cast<float>(now - m_last_ms) / 1000.0f;
    m_last_ms = now;

    const float in_db =
        peak > 0.0f ? std::clamp(20.0f * std::log10(peak), K_MIN_DB, K_MAX_DB) : K_MIN_DB;

    // Instant attack, linear-in-dB release.
    m_level_db =
        in_db >= m_level_db ? in_db : std::max(in_db, m_level_db - K_RELEASE_DB_PER_SEC * dt);

    // Peak hold: track the recent maximum, wait, then fall (never below the bar).
    if (in_db >= m_hold_db) {
        m_hold_db = in_db;
        m_hold_since_ms = now;
    } else if (now - m_hold_since_ms > K_HOLD_MS) {
        m_hold_db = std::max(m_level_db, m_hold_db - K_HOLD_FALL_DB_PER_SEC * dt);
    }

    // Repaint only on visible movement — a silent input costs nothing.
    const float level_value = to_value(m_level_db);
    const float hold_value = to_value(m_hold_db);
    if (std::abs(level_value - m_level) < 0.05f && std::abs(hold_value - m_hold) < 0.05f) {
        return;
    }
    m_level = level_value;
    m_hold = hold_value;
    update();
}

void VUMeter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Groove.
    const QRectF groove = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(Theme::track(), 1.0));
    p.setBrush(Theme::bezel());
    p.drawRoundedRect(groove, 3, 3);

    const QRectF inner = groove.adjusted(1.5, 1.5, -1.5, -1.5);
    const bool vert = orientation() == Qt::Vertical;

    // Position (px offset along the meter axis from the quiet end) of a
    // 0..100 value.
    const qreal span = vert ? inner.height() : inner.width();
    auto pos = [span](float v) {
        return span * (v / 100.0f);
    };

    // Everything below is clipped to the rounded inner region so the zone
    // rectangles keep the rounded silhouette.
    QPainterPath clip;
    clip.addRoundedRect(inner, 2, 2);
    p.setClipPath(clip);
    p.setPen(Qt::NoPen);

    // Zone fill: one rect per zone the level reaches — color by absolute
    // level, red strictly above the 0 dBFS mark.
    const float t = m_level;
    auto zone_rect = [&](float from, float to) -> QRectF {
        const qreal a = pos(from), b = pos(to);
        return vert ? QRectF(inner.left(), inner.bottom() - b, inner.width(), b - a)
                    : QRectF(inner.left() + a, inner.top(), b - a, inner.height());
    };
    if (t > 0.0f) {
        p.setBrush(Theme::meter_low());
        p.drawRect(zone_rect(0.0f, std::min(t, K_HOT_VALUE)));
    }
    if (t > K_HOT_VALUE) {
        p.setBrush(Theme::meter_mid());
        p.drawRect(zone_rect(K_HOT_VALUE, std::min(t, K_CLIP_VALUE)));
    }
    if (t > K_CLIP_VALUE) {
        p.setBrush(Theme::meter_clip());
        p.drawRect(zone_rect(K_CLIP_VALUE, t));
    }

    // 0 dBFS mark on the groove.
    {
        const qreal c = pos(K_CLIP_VALUE);
        p.setBrush(Theme::track());
        if (vert) {
            p.drawRect(QRectF(inner.left(), inner.bottom() - c - 0.5, inner.width(), 1.0));
        } else {
            p.drawRect(QRectF(inner.left() + c - 0.5, inner.top(), 1.0, inner.height()));
        }
    }

    // Peak-hold line, colored by the zone it sits in.
    if (m_hold > 0.5f) {
        p.setBrush(m_hold > K_CLIP_VALUE  ? Theme::meter_clip()
                   : m_hold > K_HOT_VALUE ? Theme::meter_mid()
                                          : Theme::meter_low());
        const qreal hp = pos(m_hold);
        if (vert) {
            p.drawRect(QRectF(inner.left(), inner.bottom() - hp - 1.0, inner.width(), 2.0));
        } else {
            p.drawRect(QRectF(inner.left() + hp - 1.0, inner.top(), 2.0, inner.height()));
        }
    }
}
