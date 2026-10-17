#include "VUMeter.h"

#include <algorithm>
#include <cmath>

#include <QPainter>

#include "Common.h"
#include "LedPaint.h"
#include "Theme.h"

namespace namx
{

VUMeter::VUMeter(Qt::Orientation orientation, int new_channel_count, QWidget* parent)
    : QProgressBar(parent)
    , channel_count(static_cast<size_t>(std::clamp(new_channel_count, 1, 2))) {
    setOrientation(orientation);
    setRange(0, 100);
    setValue(0);
    setTextVisible(false);
    if (orientation == Qt::Horizontal) {
        setFixedHeight(WIDTH);
        setMinimumWidth(70);
    } else {
        setFixedWidth(WIDTH);
        setMinimumHeight(70);
    }
}

void VUMeter::set_peak(float peak) {
    feed({peak, peak});
}

void VUMeter::set_peaks(float left, float right) {
    feed({left, right});
}

void VUMeter::feed(const std::array<float, 2>& peaks) {
    if (!clock.isValid()) {
        clock.start();
    }
    const qint64 now = clock.elapsed();
    const float dt = static_cast<float>(now - last_ms) / 1000.0f;
    last_ms = now;

    bool moved = false;
    for (size_t c = 0; c < channel_count; ++c) {
        moved |= advance(channels[c], peaks[c], now, dt);
    }
    // Repaint only on visible movement — a silent input costs nothing.
    if (moved) {
        update();
    }
}

bool VUMeter::advance(Channel& ch, float peak, qint64 now, float dt) {
    const float in_db = peak > 0.0f ? std::clamp(20.0f * std::log10(peak), MIN_DB, MAX_DB) : MIN_DB;

    // Instant attack, linear-in-dB release.
    ch.level_db =
        in_db >= ch.level_db ? in_db : std::max(in_db, ch.level_db - RELEASE_DB_PER_SEC * dt);

    // Peak hold: track the recent maximum, wait, then fall (never below the bar).
    if (in_db >= ch.hold_db) {
        ch.hold_db = in_db;
        ch.hold_since_ms = now;
    } else if (now - ch.hold_since_ms > HOLD_MS) {
        ch.hold_db = std::max(ch.level_db, ch.hold_db - HOLD_FALL_DB_PER_SEC * dt);
    }

    const float level_value = to_value(ch.level_db);
    const float hold_value = to_value(ch.hold_db);
    if (std::abs(level_value - ch.level) < 0.05f && std::abs(hold_value - ch.hold) < 0.05f) {
        return false;
    }
    ch.level = level_value;
    ch.hold = hold_value;
    return true;
}

// LED color by the zone a segment sits in: green, yellow above -6 dBFS, red
// above 0 dBFS.
QColor VUMeter::zone_color(float value) {
    return value > CLIP_VALUE  ? Theme::led_red()
           : value > HOT_VALUE ? Theme::led_amber()
                               : Theme::led_green();
}

void VUMeter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // The dark LED panel.
    const QRectF panel = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(Theme::display_line(), 1.0));
    p.setBrush(Theme::display_bg());
    p.drawRoundedRect(panel, 3, 3);

    // A column of LED segments per channel from the quiet end, side by side; the
    // margin leaves room for the glow.
    const bool vert = orientation() == Qt::Vertical;
    const QRectF leds = panel.adjusted(MARGIN, MARGIN, -MARGIN, -MARGIN);
    const qreal span = vert ? leds.height() : leds.width();
    const qreal across = vert ? leds.width() : leds.height();
    const auto lanes = static_cast<qreal>(channel_count);
    const qreal lane = (across - (lanes - 1.0) * CHANNEL_GAP) / lanes;
    const int count = std::max(1, static_cast<int>((span + LED_GAP) / (LED_SIZE + LED_GAP)));
    const qreal used = count * (LED_SIZE + LED_GAP) - LED_GAP;
    const qreal start = (span - used) / 2.0; // center the column in the panel
    auto led_rect = [&](size_t c, int i) {
        const qreal from = start + i * (LED_SIZE + LED_GAP);
        const qreal side = static_cast<qreal>(c) * (lane + CHANNEL_GAP);
        return vert ? QRectF(leds.left() + side, leds.bottom() - from - LED_SIZE, lane, LED_SIZE)
                    : QRectF(leds.left() + from, leds.top() + side, LED_SIZE, lane);
    };

    // A segment lights once the level reaches its middle; the hold LED is the
    // segment the peak-hold value falls in.
    const auto n = static_cast<float>(count);
    auto center_of = [n](int i) {
        return (static_cast<float>(i) + 0.5f) / n * 100.0f;
    };
    // Unlit first (every column), so no halo is painted over by a dark neighbour.
    vector<std::pair<size_t, int>> lit_leds; // channel, segment
    for (size_t c = 0; c < channel_count; ++c) {
        const Channel& ch = channels[c];
        const int hold_led =
            ch.hold > 0.5f ? std::min(count - 1, static_cast<int>(ch.hold / 100.0f * n)) : -1;
        for (int i = 0; i < count; ++i) {
            const float center = center_of(i);
            if (ch.level >= center || i == hold_led) {
                lit_leds.emplace_back(c, i);
            } else {
                draw_unlit_led(p, led_rect(c, i), zone_color(center), 1.0, Theme::unlit_alpha(14));
            }
        }
    }
    for (const auto& [c, i] : lit_leds) {
        const QRectF r = led_rect(c, i);
        const QColor color = zone_color(center_of(i));
        draw_lit_led(p, r, color, 1.0);
        // Hot core: lit LEDs read whiter in the middle.
        p.setBrush(color.lighter(150));
        p.drawRoundedRect(r.adjusted(r.width() * 0.25, 1.0, -r.width() * 0.25, -1.0), 1.0, 1.0);
    }
}

} // namespace namx
