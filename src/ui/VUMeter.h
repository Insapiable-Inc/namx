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
    static float to_value(float d_b) {
        return (d_b - K_MIN_DB) / (K_MAX_DB - K_MIN_DB) * 100.0f;
    }

    static constexpr float K_MIN_DB = -60.0f;
    static constexpr float K_MAX_DB = 6.0f; // headroom above the clip point
    static constexpr float K_RELEASE_DB_PER_SEC = 24.0f; // bar fall (digital peak meter)
    static constexpr qint64 K_HOLD_MS = 1000; // hold line waits…
    static constexpr float K_HOLD_FALL_DB_PER_SEC = 12.0f; // …then falls

    float m_level_db{K_MIN_DB};
    float m_hold_db{K_MIN_DB};
    qint64 m_last_ms{0};
    qint64 m_hold_since_ms{0};
    QElapsedTimer m_clock;
    float m_level{0.0f}; // painted positions, 0..100 value scale
    float m_hold{0.0f};

    // Zone boundaries on the 0..100 value scale ((dB - min) / span * 100).
    static constexpr float K_HOT_VALUE = (-6.0f - K_MIN_DB) / (K_MAX_DB - K_MIN_DB) * 100.0f;
    static constexpr float K_CLIP_VALUE = (0.0f - K_MIN_DB) / (K_MAX_DB - K_MIN_DB) * 100.0f;
};
