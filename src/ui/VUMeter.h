#pragma once

#include <array>

#include <QElapsedTimer>
#include <QProgressBar>

#include "Common.h"

namespace namx
{

// LED bar-graph peak meter (vertical fills bottom→top, horizontal fills
// left→right): a column of glowing segments on a dark panel, colored by
// absolute level like REAPER's meters — green, yellow above -6 dBFS, red
// above 0 dBFS; the scale runs -60..+6 dB so there is visible headroom above
// the clip point (float audio can exceed full scale). Unlit segments stay
// faintly visible. A single peak-hold LED marks the recent maximum and falls
// after a moment; a red hold LED means the signal clipped within the last
// second. Ballistics are time-based (instant attack, steady dB/s release,
// 1 s hold), so the look doesn't depend on how often set_peak() is called; it
// only repaints when the bar or the hold LED actually moves.
// A stereo meter draws two columns side by side in the same panel, left then right
// (left/top first for a horizontal one), each with its own ballistics.
class VUMeter : public QProgressBar
{
    Q_OBJECT
public:
    explicit VUMeter(Qt::Orientation orientation = Qt::Vertical, int channel_count = 1,
                     QWidget* parent = nullptr);

    // Linear peaks (0..1+); 1.0 reaches the 0 dBFS mark. set_peak feeds every column
    // (a mono meter), set_peaks a stereo one.
    void set_peak(float peak);
    void set_peaks(float left, float right);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // One column's ballistics and painted positions.
    struct Channel
    {
        float level_db{MIN_DB};
        float hold_db{MIN_DB};
        qint64 hold_since_ms{0};
        float level{0.0f}; // painted positions, 0..100 value scale
        float hold{0.0f};
    };

    // Advances `ch` to the new peak; true when its painted position moved.
    static bool advance(Channel& ch, float peak, qint64 now, float dt);
    void feed(const std::array<float, 2>& peaks);
    static QColor zone_color(float value);
    static float to_value(float db) {
        return (db - MIN_DB) / (MAX_DB - MIN_DB) * 100.0f;
    }

    static constexpr int WIDTH = 36;
    static constexpr qreal MARGIN = 6.0; // panel edge to the LEDs: room for the glow
    static constexpr qreal LED_SIZE = 4.0; // along the meter axis
    static constexpr qreal LED_GAP = 2.0;
    static constexpr qreal CHANNEL_GAP = 2.0; // between the left and right columns
    static constexpr float MIN_DB = -60.0f;
    static constexpr float MAX_DB = 6.0f; // headroom above the clip point
    static constexpr float RELEASE_DB_PER_SEC = 24.0f; // bar fall (digital peak meter)
    static constexpr qint64 HOLD_MS = 1000; // hold line waits…
    static constexpr float HOLD_FALL_DB_PER_SEC = 12.0f; // …then falls

    size_t channel_count; // 1 or 2
    std::array<Channel, 2> channels;
    qint64 last_ms{0};
    QElapsedTimer clock;

    // Zone boundaries on the 0..100 value scale ((dB - min) / span * 100).
    static constexpr float HOT_VALUE = (-6.0f - MIN_DB) / (MAX_DB - MIN_DB) * 100.0f;
    static constexpr float CLIP_VALUE = (0.0f - MIN_DB) / (MAX_DB - MIN_DB) * 100.0f;
};

} // namespace namx
