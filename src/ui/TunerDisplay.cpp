#include "TunerDisplay.h"
#include "Theme.h"

#include <QFont>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace {

// Seven-segment bits: A top, B top-right, C bottom-right, D bottom,
// E bottom-left, F top-left, G middle.
enum Seg { SA = 1, SB = 2, SC = 4, SD = 8, SE = 16, SF = 32, SG = 64 };

int segmentsFor(QChar letter)
{
    switch (letter.toUpper().unicode())
    {
    case 'A': return SA | SB | SC | SE | SF | SG;
    case 'B': return SC | SD | SE | SF | SG;      // lowercase b
    case 'C': return SA | SD | SE | SF;
    case 'D': return SB | SC | SD | SE | SG;      // lowercase d
    case 'E': return SA | SD | SE | SF | SG;
    case 'F': return SA | SE | SF | SG;
    case 'G': return SA | SC | SD | SE | SF;
    default:  return SG;                          // "-" placeholder
    }
}

// One bloom-styled rounded rect: soft halo passes, then the solid core.
void drawGlowRect(QPainter& p, const QRectF& r, const QColor& c, qreal radius)
{
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
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void TunerDisplay::setReading(const QString& note, float cents, bool valid)
{
    const bool sameCents =
        std::lround(cents) == std::lround(mCents);
    if (valid == mValid && note == mNote && (sameCents || !valid))
        return;
    mNote  = note;
    mCents = cents;
    mValid = valid;
    update();
}

void TunerDisplay::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // The LED panel.
    const QRectF panel = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(Theme::track(), 1.0));
    p.setBrush(Theme::displayBg());
    p.drawRoundedRect(panel, 5, 5);

    const bool inTune = mValid && std::abs(mCents) <= kInTuneCents;
    const QColor green = Theme::ledGreen();
    const QColor red   = Theme::ledRed();

    // ---- LED ladder across the top ----
    {
        const qreal margin = 16.0;
        const qreal ledD   = 7.0;
        const qreal y      = panel.top() + 8.0;
        const qreal step   = (panel.width() - 2 * margin - ledD) / (kLedCount - 1);
        const int   center = kLedCount / 2;

        // Offset LED index for the current detune (at least one step off
        // center when audibly out of tune).
        int target = center;
        if (mValid && !inTune)
        {
            const float span = std::clamp(mCents, -50.0f, 50.0f) / 50.0f;
            int off = static_cast<int>(std::lround(span * center));
            if (off == 0) off = mCents < 0 ? -1 : 1;
            target = center + off;
        }

        for (int i = 0; i < kLedCount; ++i)
        {
            const QRectF r(panel.left() + margin + i * step, y, ledD, ledD);
            const QColor base = (i == center) ? green : red;
            bool lit = false;
            if (mValid)
            {
                if (inTune)          lit = (i == center);
                else if (mCents < 0) lit = (i >= target && i < center);
                else                 lit = (i > center && i <= target);
            }
            if (lit)
            {
                drawGlowRect(p, r, base, ledD / 2.0);
            }
            else
            {
                QColor ghost = base;
                ghost.setAlpha(30);
                p.setPen(Qt::NoPen);
                p.setBrush(ghost);
                p.drawEllipse(r);
            }
        }
    }

    // ---- Seven-segment note in the center ----
    const QColor digitColor = !mValid ? Theme::track().lighter(150)
                            : inTune  ? green
                                      : red;
    const qreal W = 22.0, H = 34.0, t = 4.4;
    const qreal half   = (H - 3 * t) / 2.0;
    const qreal digitX = panel.center().x() - W / 2.0 - 12.0;
    const qreal digitY = panel.top() + 21.0;

    const struct { Seg seg; QRectF r; } segs[] = {
        {SA, {digitX + t, digitY, W - 2 * t, t}},
        {SG, {digitX + t, digitY + t + half, W - 2 * t, t}},
        {SD, {digitX + t, digitY + 2 * (t + half), W - 2 * t, t}},
        {SF, {digitX, digitY + t, t, half}},
        {SB, {digitX + W - t, digitY + t, t, half}},
        {SE, {digitX, digitY + 2 * t + half, t, half}},
        {SC, {digitX + W - t, digitY + 2 * t + half, t, half}},
    };

    const int lit = segmentsFor(mNote.isEmpty() ? QChar('-') : mNote.at(0));
    for (const auto& s : segs)
    {
        if (lit & s.seg)
        {
            drawGlowRect(p, s.r, digitColor, t / 2.0);
        }
        else
        {
            QColor ghost = digitColor;
            ghost.setAlpha(22);
            p.setPen(Qt::NoPen);
            p.setBrush(ghost);
            p.drawRoundedRect(s.r, t / 2.0, t / 2.0);
        }
    }

    // Sharp marker at the digit's top right.
    if (mValid && mNote.contains('#'))
    {
        QFont f = font();
        f.setPixelSize(14);
        f.setBold(true);
        p.setFont(f);
        const QRectF sharpRect(digitX + W + 3.0, digitY - 4.0, 16.0, 16.0);
        QColor halo = digitColor;
        halo.setAlpha(60);
        p.setPen(halo);
        for (const QPointF d : {QPointF(1, 0), QPointF(-1, 0), QPointF(0, 1), QPointF(0, -1)})
            p.drawText(sharpRect.translated(d), Qt::AlignCenter, "#");
        p.setPen(digitColor);
        p.drawText(sharpRect, Qt::AlignCenter, "#");
    }

    // ---- Signed cents readout, right of the digit ----
    {
        QFont f = font();
        f.setPointSizeF(10.5);
        f.setBold(true);
        p.setFont(f);
        p.setPen(digitColor);
        const int c = static_cast<int>(std::lround(mCents));
        const QString text = !mValid
            ? QStringLiteral("--")
            : QString("%1%2¢").arg(c > 0 ? "+" : "").arg(c);
        const QRectF textRect(panel.center().x() + 26.0, digitY, 90.0, H);
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
    }
}
