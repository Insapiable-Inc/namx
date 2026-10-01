#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QWidget>

#include "Common.h"

namespace npl
{

/// Gain-reduction meter for the Compressor / Gate rows: a caption with the
/// current reduction in dB over a thin bar that fills right → left (the
/// console convention: reduction "pulls" the level down). Instant attack,
/// time-based release; repaints only when the reading moves.
class GainReductionMeter : public QWidget
{
    Q_OBJECT
public:
    /// `rangeDb` = reduction at full bar; `accent` = the card's color.
    GainReductionMeter(float new_range_db, const QColor& new_accent, QWidget* parent = nullptr);

    /// Deepest reduction since the last call (dB, ≥ 0).
    void set_reduction(float new_db);

    QSize sizeHint() const override {
        return {90, 34};
    } // fits the stereo slot

protected:
    void paintEvent(QPaintEvent* ev) override;

private:
    static constexpr float RELEASE_DB_PER_SEC = 40.0f;

    float range_db;
    QColor accent;
    float db{0.0f}; // after ballistics
    float shown_db{-1.0f};
    qint64 last_ms{0};
    QElapsedTimer clock;
};

} // namespace npl
