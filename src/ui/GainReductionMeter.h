#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QWidget>

/// Gain-reduction meter for the Compressor / Gate rows: a caption with the
/// current reduction in dB over a thin bar that fills right → left (the
/// console convention: reduction "pulls" the level down). Instant attack,
/// time-based release; repaints only when the reading moves.
class GainReductionMeter : public QWidget
{
    Q_OBJECT
public:
    /// `rangeDb` = reduction at full bar; `accent` = the card's color.
    GainReductionMeter(float rangeDb, const QColor& accent, QWidget* parent = nullptr);

    /// Deepest reduction since the last call (dB, ≥ 0).
    void setReduction(float dB);

    QSize sizeHint() const override {
        return {90, 34};
    } // fits the stereo slot

protected:
    void paintEvent(QPaintEvent* ev) override;

private:
    static constexpr float kReleaseDbPerSec = 40.0f;

    float mRangeDb;
    QColor mAccent;
    float mDb{0.0f}; // after ballistics
    float mShownDb{-1.0f};
    qint64 mLastMs{0};
    QElapsedTimer mClock;
};
