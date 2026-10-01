#include "GainReductionMeter.h"

#include <algorithm>
#include <cmath>

#include <QPainter>

#include "Theme.h"

GainReductionMeter::GainReductionMeter(float range_db, const QColor& accent, QWidget* parent)
    : QWidget(parent)
    , m_range_db(range_db)
    , m_accent(accent) {
    setFixedSize(sizeHint());
    setToolTip(QString("Gain reduction (full bar = %1 dB)").arg(range_db, 0, 'f', 0));
}

void GainReductionMeter::set_reduction(float d_b) {
    if (!m_clock.isValid()) {
        m_clock.start();
    }
    const qint64 now = m_clock.elapsed();
    const float dt = static_cast<float>(now - m_last_ms) / 1000.0f;
    m_last_ms = now;

    d_b = std::clamp(d_b, 0.0f, m_range_db);
    m_db = d_b >= m_db ? d_b : std::max(d_b, m_db - K_RELEASE_DB_PER_SEC * dt);

    // Repaint on a visible change only: a tenth of a dB in the caption or a
    // fraction of a pixel on the bar.
    if (std::abs(m_db - m_shown_db) < 0.05f) {
        return;
    }
    m_shown_db = m_db;
    update();
}

void GainReductionMeter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Caption: "GR" left, value right — knob-label typography.
    QFont f = font();
    f.setPointSizeF(8.0);
    p.setFont(f);
    const QRectF text(0, 0, width(), 16);
    p.setPen(Theme::dim_text());
    p.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, "GR");
    f.setBold(true);
    p.setFont(f);
    p.setPen(m_db >= 0.1f ? m_accent : Theme::dim_text());
    p.drawText(text, Qt::AlignRight | Qt::AlignVCenter,
               m_db >= 0.1f ? QString("−%1 dB").arg(m_db, 0, 'f', 1) : QString("0 dB"));

    // Bar: groove, then the reduction filled from the right edge.
    const QRectF groove(0.5, 21.5, width() - 1.0, 8.0);
    p.setPen(QPen(Theme::track(), 1.0));
    p.setBrush(Theme::bezel());
    p.drawRoundedRect(groove, 3, 3);

    const qreal frac = m_db / m_range_db;
    if (frac > 0.0) {
        const QRectF inner = groove.adjusted(1.5, 1.5, -1.5, -1.5);
        const qreal w = inner.width() * frac;
        p.setPen(Qt::NoPen);
        p.setBrush(m_accent);
        p.drawRoundedRect(QRectF(inner.right() - w, inner.top(), w, inner.height()), 1.5, 1.5);
    }
}
