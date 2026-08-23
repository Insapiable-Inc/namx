#pragma once

#include <QProgressBar>

/// VU bar (vertical fills bottom→top, horizontal fills left→right).
/// setPeak() maps a linear peak sample to the bar's 0–100 range on a 60 dB
/// scale. Custom-painted: the green→red gradient is anchored to the full bar,
/// so a color always means the same absolute level (red = near clipping),
/// unlike a stylesheet chunk gradient which stretches with the fill.
class VUMeter : public QProgressBar
{
    Q_OBJECT
public:
    explicit VUMeter(Qt::Orientation orientation = Qt::Vertical,
                     QWidget* parent = nullptr);

    /// Linear peak (0..1+); 0 empties the bar, 1.0 fills it.
    void setPeak(float peak);

protected:
    void paintEvent(QPaintEvent* event) override;
};
