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
    void set_peak(float peak);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    static float to_value(float db) {
        return (db - MIN_DB) / (MAX_DB - MIN_DB) * 100.0f;
    }

    static constexpr float MIN_DB = -60.0f;
    static constexpr float MAX_DB = 6.0f; // headroom above the clip point
    static constexpr float RELEASE_DB_PER_SEC = 24.0f; // bar fall (digital peak meter)
    static constexpr qint64 HOLD_MS = 1000; // hold line waits…
    static constexpr float HOLD_FALL_DB_PER_SEC = 12.0f; // …then falls

    float level_db{MIN_DB};
    float hold_db{MIN_DB};
    qint64 last_ms{0};
    qint64 hold_since_ms{0};
    QElapsedTimer clock;
    float level{0.0f}; // painted positions, 0..100 value scale
    float hold{0.0f};

    // Zone boundaries on the 0..100 value scale ((dB - min) / span * 100).
    static constexpr float HOT_VALUE = (-6.0f - MIN_DB) / (MAX_DB - MIN_DB) * 100.0f;
    static constexpr float CLIP_VALUE = (0.0f - MIN_DB) / (MAX_DB - MIN_DB) * 100.0f;
};
