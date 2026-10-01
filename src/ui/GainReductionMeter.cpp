#include "GainReductionMeter.h"

#include <algorithm>
#include <cmath>

#include <QPainter>

#include "Theme.h"

GainReductionMeter::GainReductionMeter(float rangeDb, const QColor& accent, QWidget* parent)
    : QWidget(parent)
    , mRangeDb(rangeDb)
    , mAccent(accent) {
    setFixedSize(sizeHint());
    setToolTip(QString("Gain reduction (full bar = %1 dB)").arg(rangeDb, 0, 'f', 0));
}

void GainReductionMeter::setReduction(float dB) {
    if (!mClock.isValid()) {
        mClock.start();
    }
    const qint64 now = mClock.elapsed();
    const float dt = static_cast<float>(now - mLastMs) / 1000.0f;
    mLastMs = now;

    dB = std::clamp(dB, 0.0f, mRangeDb);
    mDb = dB >= mDb ? dB : std::max(dB, mDb - kReleaseDbPerSec * dt);

    // Repaint on a visible change only: a tenth of a dB in the caption or a
    // fraction of a pixel on the bar.
    if (std::abs(mDb - mShownDb) < 0.05f) {
        return;
    }
    mShownDb = mDb;
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
    p.setPen(Theme::dimText());
    p.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, "GR");
    f.setBold(true);
    p.setFont(f);
    p.setPen(mDb >= 0.1f ? mAccent : Theme::dimText());
    p.drawText(text, Qt::AlignRight | Qt::AlignVCenter,
               mDb >= 0.1f ? QString("−%1 dB").arg(mDb, 0, 'f', 1) : QString("0 dB"));

    // Bar: groove, then the reduction filled from the right edge.
    const QRectF groove(0.5, 21.5, width() - 1.0, 8.0);
    p.setPen(QPen(Theme::track(), 1.0));
    p.setBrush(Theme::bezel());
    p.drawRoundedRect(groove, 3, 3);

    const qreal frac = mDb / mRangeDb;
    if (frac > 0.0) {
        const QRectF inner = groove.adjusted(1.5, 1.5, -1.5, -1.5);
        const qreal w = inner.width() * frac;
        p.setPen(Qt::NoPen);
        p.setBrush(mAccent);
        p.drawRoundedRect(QRectF(inner.right() - w, inner.top(), w, inner.height()), 1.5, 1.5);
    }
}
