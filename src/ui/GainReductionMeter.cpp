#include "GainReductionMeter.h"

#include <algorithm>
#include <cmath>

#include <QPainter>

#include "Common.h"
#include "Theme.h"

namespace npl
{

GainReductionMeter::GainReductionMeter(float new_range_db, const QColor& new_accent,
                                       QWidget* parent)
    : QWidget(parent)
    , range_db(new_range_db)
    , accent(new_accent) {
    setFixedSize(sizeHint());
    setToolTip(QString("Gain reduction (full bar = %1 dB)").arg(new_range_db, 0, 'f', 0));
}

void GainReductionMeter::set_reduction(float new_db) {
    if (!clock.isValid()) {
        clock.start();
    }
    const qint64 now = clock.elapsed();
    const float dt = static_cast<float>(now - last_ms) / 1000.0f;
    last_ms = now;

    new_db = std::clamp(new_db, 0.0f, range_db);
    db = new_db >= db ? new_db : std::max(new_db, db - RELEASE_DB_PER_SEC * dt);

    // Repaint on a visible change only: a tenth of a dB in the caption or a
    // fraction of a pixel on the bar.
    if (std::abs(db - shown_db) < 0.05f) {
        return;
    }
    shown_db = db;
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
    p.setPen(db >= 0.1f ? accent : Theme::dim_text());
    p.drawText(text, Qt::AlignRight | Qt::AlignVCenter,
               db >= 0.1f ? QString("−%1 dB").arg(db, 0, 'f', 1) : QString("0 dB"));

    // Bar: groove, then the reduction filled from the right edge.
    const QRectF groove(0.5, 21.5, width() - 1.0, 8.0);
    p.setPen(QPen(Theme::track(), 1.0));
    p.setBrush(Theme::bezel());
    p.drawRoundedRect(groove, 3, 3);

    const qreal frac = db / range_db;
    if (frac > 0.0) {
        const QRectF inner = groove.adjusted(1.5, 1.5, -1.5, -1.5);
        const qreal w = inner.width() * frac;
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawRoundedRect(QRectF(inner.right() - w, inner.top(), w, inner.height()), 1.5, 1.5);
    }
}

} // namespace npl
