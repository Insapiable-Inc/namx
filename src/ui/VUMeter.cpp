#include "VUMeter.h"

#include <algorithm>
#include <cmath>

#include <QPainter>

#include "Common.h"
#include "LedPaint.h"
#include "Theme.h"

namespace namx
{

VUMeter::VUMeter(Qt::Orientation orientation, QWidget* parent)
    : QProgressBar(parent) {
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
    if (!clock.isValid()) {
        clock.start();
    }
    const qint64 now = clock.elapsed();
    const float dt = static_cast<float>(now - last_ms) / 1000.0f;
    last_ms = now;

    const float in_db = peak > 0.0f ? std::clamp(20.0f * std::log10(peak), MIN_DB, MAX_DB) : MIN_DB;

    // Instant attack, linear-in-dB release.
    level_db = in_db >= level_db ? in_db : std::max(in_db, level_db - RELEASE_DB_PER_SEC * dt);

    // Peak hold: track the recent maximum, wait, then fall (never below the bar).
    if (in_db >= hold_db) {
        hold_db = in_db;
        hold_since_ms = now;
    } else if (now - hold_since_ms > HOLD_MS) {
        hold_db = std::max(level_db, hold_db - HOLD_FALL_DB_PER_SEC * dt);
    }

    // Repaint only on visible movement — a silent input costs nothing.
    const float level_value = to_value(level_db);
    const float hold_value = to_value(hold_db);
    if (std::abs(level_value - level) < 0.05f && std::abs(hold_value - hold) < 0.05f) {
        return;
    }
    level = level_value;
    hold = hold_value;
    update();
}

// LED color by the zone a segment sits in: green, yellow above -6 dBFS, red
// above 0 dBFS.
QColor VUMeter::zone_color(float value) {
    return value > CLIP_VALUE  ? Theme::led_red()
           : value > HOT_VALUE ? Theme::meter_mid()
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

    // A column of LED segments from the quiet end; the margin leaves room for the glow.
    const bool vert = orientation() == Qt::Vertical;
    const QRectF leds = panel.adjusted(MARGIN, MARGIN, -MARGIN, -MARGIN);
    const qreal span = vert ? leds.height() : leds.width();
    const int count = std::max(1, static_cast<int>((span + LED_GAP) / (LED_SIZE + LED_GAP)));
    const qreal used = count * (LED_SIZE + LED_GAP) - LED_GAP;
    const qreal start = (span - used) / 2.0; // center the column in the panel
    auto led_rect = [&](int i) {
        const qreal from = start + i * (LED_SIZE + LED_GAP);
        return vert ? QRectF(leds.left(), leds.bottom() - from - LED_SIZE, leds.width(), LED_SIZE)
                    : QRectF(leds.left() + from, leds.top(), LED_SIZE, leds.height());
    };

    // A segment lights once the level reaches its middle; the hold LED is the
    // segment the peak-hold value falls in.
    const auto n = static_cast<float>(count);
    auto center_of = [n](int i) {
        return (static_cast<float>(i) + 0.5f) / n * 100.0f;
    };
    const int hold_led =
        hold > 0.5f ? std::min(count - 1, static_cast<int>(hold / 100.0f * n)) : -1;
    // Unlit first, so no halo is painted over by a dark neighbour.
    vector<int> lit_leds;
    for (int i = 0; i < count; ++i) {
        const float center = center_of(i);
        if (level >= center || i == hold_led) {
            lit_leds.push_back(i);
        } else {
            draw_unlit_led(p, led_rect(i), zone_color(center), 1.0);
        }
    }
    for (const int i : lit_leds) {
        const QRectF r = led_rect(i);
        const QColor c = zone_color(center_of(i));
        draw_lit_led(p, r, c, 1.0);
        // Hot core: lit LEDs read whiter in the middle.
        p.setBrush(c.lighter(150));
        p.drawRoundedRect(r.adjusted(r.width() * 0.25, 1.0, -r.width() * 0.25, -1.0), 1.0, 1.0);
    }
}

} // namespace namx
