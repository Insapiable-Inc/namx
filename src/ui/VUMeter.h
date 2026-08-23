#pragma once

#include <QProgressBar>

/// Peak meter (vertical fills bottom→top, horizontal fills left→right),
/// colored by absolute level like REAPER's meters: green fill, yellow only
/// for the part above -12 dBFS, and red only for the part above 0 dBFS —
/// the scale runs -60..+6 dB so there is visible headroom above the clip
/// point (float audio can exceed full scale). A peak-hold line marks the
/// recent maximum and falls after a moment; it takes its zone's color, so a
/// red hold line means the signal clipped within the last second.
/// setPeak() feeds a linear peak sample and drives the hold timing, so call
/// it at a steady rate (the 50 ms meter timer).
class VUMeter : public QProgressBar
{
    Q_OBJECT
public:
    explicit VUMeter(Qt::Orientation orientation = Qt::Vertical,
                     QWidget* parent = nullptr);

    /// Linear peak (0..1+); 1.0 reaches the 0 dBFS mark.
    void setPeak(float peak);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    float mHold{0.0f}; // peak-hold position, 0..100 (value scale)
    int   mHoldAge{0}; // ticks since the hold was last raised

    static constexpr float kMinDb = -60.0f;
    static constexpr float kMaxDb =   6.0f; // headroom above the clip point
    static constexpr int   kHoldTicks = 20;   // ~1 s before the hold falls
    static constexpr float kHoldFall  = 2.5f; // fall speed per tick

    // Zone boundaries on the 0..100 value scale ((dB - min) / span * 100).
    static constexpr float kHotValue  = (-12.0f - kMinDb) / (kMaxDb - kMinDb) * 100.0f;
    static constexpr float kClipValue = (  0.0f - kMinDb) / (kMaxDb - kMinDb) * 100.0f;
};
