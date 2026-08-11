#pragma once

#include <QProgressBar>

/// Vertical VU bar. setPeak() maps a linear peak sample to the bar's 0–100
/// range on a 60 dB scale. Colors come from the Theme stylesheet via the
/// "vuMeter" objectName.
class VUMeter : public QProgressBar
{
    Q_OBJECT
public:
    explicit VUMeter(QWidget* parent = nullptr);

    /// Linear peak (0..1+); 0 empties the bar, 1.0 hits the top.
    void setPeak(float peak);
};
