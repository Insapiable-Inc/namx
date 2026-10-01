#include "TunerDisplay.h"

#include <algorithm>
#include <cmath>

#include <QFont>
#include <QPainter>

#include "Theme.h"

namespace
{

// Seven-segment bits: A top, B top-right, C bottom-right, D bottom,
// E bottom-left, F top-left, G middle.
enum Seg
{
    SA = 1,
    SB = 2,
    SC = 4,
    SD = 8,
    SE = 16,
    SF = 32,
    SG = 64
};

int segments_for(QChar letter) {
    switch (letter.toUpper().unicode()) {
        case 'A':
            return SA | SB | SC | SE | SF | SG;
        case 'B':
            return SC | SD | SE | SF | SG; // lowercase b
        case 'C':
            return SA | SD | SE | SF;
        case 'D':
            return SB | SC | SD | SE | SG; // lowercase d
        case 'E':
            return SA | SD | SE | SF | SG;
        case 'F':
            return SA | SE | SF | SG;
        case 'G':
            return SA | SC | SD | SE | SF;
        default:
            return SG; // "-" placeholder
    }
}

// One bloom-styled rounded rect: soft halo passes, then the solid core.
void draw_glow_rect(QPainter& p, const QRectF& r, const QColor& c, qreal radius) {
    QColor halo = c;
    p.setPen(Qt::NoPen);
    halo.setAlpha(28);
    p.setBrush(halo);
    p.drawRoundedRect(r.adjusted(-3.0, -3.0, 3.0, 3.0), radius + 3.0, radius + 3.0);
    halo.setAlpha(70);
    p.setBrush(halo);
    p.drawRoundedRect(r.adjusted(-1.2, -1.2, 1.2, 1.2), radius + 1.2, radius + 1.2);
    p.setBrush(c);
    p.drawRoundedRect(r, radius, radius);
}

} // namespace

TunerDisplay::TunerDisplay(QWidget* parent)
    : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void TunerDisplay::set_reading(const QString& note, float cents, bool valid, float hz) {
    const bool same_cents = std::lround(cents) == std::lround(m_cents);
    const bool same_hz = !m_show_hz || std::lround(hz * 10.0f) == std::lround(m_hz * 10.0f);
    if (valid == m_valid && note == m_note && ((same_cents && same_hz) || !valid)) {
        return;
    }
    m_note = note;
    m_cents = cents;
    m_valid = valid;
    m_hz = hz;
    update();
}

void TunerDisplay::set_show_hz(bool on) {
    if (on == m_show_hz) {
        return;
    }
    m_show_hz = on;
    update();
}

void TunerDisplay::set_reference(int hz) {
    if (hz == m_reference) {
        return;
    }
    m_reference = hz;
    update();
}

void TunerDisplay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // The LED panel.
    const QRectF panel = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(Theme::track(), 1.0));
    p.setBrush(Theme::display_bg());
    p.drawRoundedRect(panel, 5, 5);

    const bool in_tune = m_valid && std::abs(m_cents) <= K_IN_TUNE_CENTS;
    const QColor green = Theme::led_green();
    const QColor red = Theme::led_red();

    // ---- LED ladder across the top ----
    {
        const qreal margin = 14.0;
        const qreal led_d = 6.0;
        const qreal y = panel.top() + 5.0;
        const qreal step = (panel.width() - 2 * margin - led_d) / (K_LED_COUNT - 1);
        const int center = K_LED_COUNT / 2;

        // Offset LED index for the current detune (at least one step off
        // center when audibly out of tune).
        int target = center;
        if (m_valid && !in_tune) {
            const float span = std::clamp(m_cents, -50.0f, 50.0f) / 50.0f;
            int off = static_cast<int>(std::lround(span * center));
            if (off == 0) {
                off = m_cents < 0 ? -1 : 1;
            }
            target = center + off;
        }

        for (int i = 0; i < K_LED_COUNT; ++i) {
            const QRectF r(panel.left() + margin + i * step, y, led_d, led_d);
            const QColor base = (i == center) ? green : red;
            bool lit = false;
            if (m_valid) {
                if (in_tune) {
                    lit = (i == center);
                } else if (m_cents < 0) {
                    lit = (i >= target && i < center);
                } else {
                    lit = (i > center && i <= target);
                }
            }
            if (lit) {
                draw_glow_rect(p, r, base, led_d / 2.0);
            } else {
                QColor ghost = base;
                ghost.setAlpha(30);
                p.setPen(Qt::NoPen);
                p.setBrush(ghost);
                p.drawEllipse(r);
            }
        }
    }

    // ---- Seven-segment note in the center ----
    const QColor digit_color = !m_valid ? Theme::track().lighter(150) : in_tune ? green : red;
    const qreal w = 18.0, h = 26.0, t = 3.5;
    const qreal half = (h - 3 * t) / 2.0;
    const qreal digit_x = panel.center().x() - w / 2.0 - 12.0;
    const qreal digit_y = panel.top() + 15.0;

    const struct
    {
        Seg seg;
        QRectF r;
    } segs[] = {
        {SA, {digit_x + t, digit_y, w - 2 * t, t}},
        {SG, {digit_x + t, digit_y + t + half, w - 2 * t, t}},
        {SD, {digit_x + t, digit_y + 2 * (t + half), w - 2 * t, t}},
        {SF, {digit_x, digit_y + t, t, half}},
        {SB, {digit_x + w - t, digit_y + t, t, half}},
        {SE, {digit_x, digit_y + 2 * t + half, t, half}},
        {SC, {digit_x + w - t, digit_y + 2 * t + half, t, half}},
    };

    const int lit = segments_for(m_note.isEmpty() ? QChar('-') : m_note.at(0));
    for (const auto& s : segs) {
        if (lit & s.seg) {
            draw_glow_rect(p, s.r, digit_color, t / 2.0);
        } else {
            QColor ghost = digit_color;
            ghost.setAlpha(22);
            p.setPen(Qt::NoPen);
            p.setBrush(ghost);
            p.drawRoundedRect(s.r, t / 2.0, t / 2.0);
        }
    }

    // Sharp marker at the digit's top right.
    if (m_valid && m_note.contains('#')) {
        QFont f = font();
        f.setPixelSize(12);
        f.setBold(true);
        p.setFont(f);
        const QRectF sharp_rect(digit_x + w + 3.0, digit_y - 3.0, 14.0, 14.0);
        QColor halo = digit_color;
        halo.setAlpha(60);
        p.setPen(halo);
        for (const QPointF d : {QPointF(1, 0), QPointF(-1, 0), QPointF(0, 1), QPointF(0, -1)}) {
            p.drawText(sharp_rect.translated(d), Qt::AlignCenter, "#");
        }
        p.setPen(digit_color);
        p.drawText(sharp_rect, Qt::AlignCenter, "#");
    }

    // ---- Signed cents readout, right of the digit ----
    {
        QFont f = font();
        f.setPointSizeF(10.0);
        f.setBold(true);
        p.setFont(f);
        p.setPen(digit_color);
        const int c = static_cast<int>(std::lround(m_cents));
        const QString text =
            !m_valid ? QStringLiteral("--") : QString("%1%2¢").arg(c > 0 ? "+" : "").arg(c);
        const QRectF text_rect(panel.center().x() + 24.0, digit_y, 90.0, h);
        p.drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter, text);
    }

    // ---- Left of the digit: detected Hz, or the reference when that's off ----
    {
        QFont f = font();
        f.setPointSizeF(m_show_hz ? 10.0 : 8.5);
        f.setBold(m_show_hz);
        p.setFont(f);
        const QRectF text_rect(panel.center().x() - 24.0 - 12.0 - 100.0, digit_y, 100.0, h);
        if (m_show_hz) {
            p.setPen(digit_color);
            p.drawText(text_rect, Qt::AlignRight | Qt::AlignVCenter,
                       m_valid ? QString("%1 Hz").arg(m_hz, 0, 'f', 1) : QStringLiteral("-- Hz"));
        } else {
            p.setPen(Theme::track().lighter(170));
            p.drawText(text_rect, Qt::AlignRight | Qt::AlignVCenter,
                       QString("A4 = %1").arg(m_reference));
        }
    }
}
