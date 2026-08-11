#include "VUMeter.h"

#include <algorithm>
#include <cmath>

VUMeter::VUMeter(QWidget* parent)
    : QProgressBar(parent)
{
    setObjectName("vuMeter"); // chunk gradient lives in the Theme stylesheet
    setOrientation(Qt::Vertical);
    setRange(0, 100);
    setValue(0);
    setTextVisible(false);
    setFixedWidth(14);
    setMinimumHeight(70);
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
