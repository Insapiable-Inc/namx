#pragma once

#include <QElapsedTimer>
#include <QProgressBar>

/// Peak meter (vertical fills bottom→top, horizontal fills left→right),
/// colored by absolute level like REAPER's meters: green fill, yellow only
/// for the part above -6 dBFS, and red only for the part above 0 dBFS;
/// the scale runs -60..+6 dB so there is visible headroom above the clip
/// point (float audio can exceed full scale). A peak-hold line marks the
/// recent maximum and falls after a moment; it takes its zone's color, so a
/// red hold line means the signal clipped within the last second.
/// Ballistics are time-based (instant attack, steady dB/s release, 1 s hold),
/// so the look doesn't depend on how often setPeak() is called; it only
/// repaints when the bar or the hold line actually moves.
class VUMeter : public QProgressBar
{
    Q_OBJECT
public:
    explicit VUMeter(Qt::Orientation orientation = Qt::Vertical, QWidget* parent = nullptr);

    /// Linear peak (0..1+); 1.0 reaches the 0 dBFS mark.
    void setPeak(float peak);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    static float toValue(float dB) {
        return (dB - kMinDb) / (kMaxDb - kMinDb) * 100.0f;
    }

    static constexpr float kMinDb = -60.0f;
    static constexpr float kMaxDb = 6.0f; // headroom above the clip point
    static constexpr float kReleaseDbPerSec = 24.0f; // bar fall (digital peak meter)
    static constexpr qint64 kHoldMs = 1000; // hold line waits…
    static constexpr float kHoldFallDbPerSec = 12.0f; // …then falls

    float mLevelDb{kMinDb};
    float mHoldDb{kMinDb};
    qint64 mLastMs{0};
    qint64 mHoldSinceMs{0};
    QElapsedTimer mClock;
    float mLevel{0.0f}; // painted positions, 0..100 value scale
    float mHold{0.0f};

    // Zone boundaries on the 0..100 value scale ((dB - min) / span * 100).
    static constexpr float kHotValue = (-6.0f - kMinDb) / (kMaxDb - kMinDb) * 100.0f;
    static constexpr float kClipValue = (0.0f - kMinDb) / (kMaxDb - kMinDb) * 100.0f;
};
