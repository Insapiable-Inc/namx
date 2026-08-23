#include "VUMeter.h"
#include "Theme.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

VUMeter::VUMeter(Qt::Orientation orientation, QWidget* parent)
    : QProgressBar(parent)
{
    setOrientation(orientation);
    setRange(0, 100);
    setValue(0);
    setTextVisible(false);
    if (orientation == Qt::Horizontal)
    {
        setFixedHeight(14);
        setMinimumWidth(70);
    }
    else
    {
        setFixedWidth(14);
        setMinimumHeight(70);
    }
}

void VUMeter::setPeak(float peak)
{
    if (peak <= 0.0f)
    {
        setValue(0);
        return;
    }
    const float dB = 20.0f * std::log10(peak);
    setValue(std::max(0, std::min(100, static_cast<int>((dB + 60.0f) / 60.0f * 100.0f))));
}

void VUMeter::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Groove.
    const QRectF groove = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(Theme::track(), 1.0));
    p.setBrush(Theme::bezel());
    p.drawRoundedRect(groove, 3, 3);

    const double t = (value() - minimum())
                     / std::max(1.0, double(maximum() - minimum()));
    if (t <= 0.0) return;

    // Fill, clipped at the level. The gradient spans the whole groove so the
    // red end only shows when the signal actually reaches it.
    QRectF fill = groove.adjusted(1.5, 1.5, -1.5, -1.5);
    QLinearGradient g;
    if (orientation() == Qt::Vertical)
    {
        g = QLinearGradient(fill.bottomLeft(), fill.topLeft());
        fill.setTop(fill.bottom() - fill.height() * t);
    }
    else
    {
        g = QLinearGradient(fill.topLeft(), fill.topRight());
        fill.setRight(fill.left() + fill.width() * t);
    }
    g.setColorAt(0.00, Theme::meterLow());
    g.setColorAt(0.65, Theme::meterMid());
    g.setColorAt(0.90, Theme::meterHigh());
    g.setColorAt(1.00, Theme::meterClip());

    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(fill, 2, 2);
}
