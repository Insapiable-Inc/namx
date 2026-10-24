#pragma once

#include <QDoubleSpinBox>

#include "Common.h"

namespace namx
{

// The desktop's own spin box with a unit: "120 BPM". Arrows, scroll, keys or type
// (Enter or leaving the field applies); Shift makes a step ten times finer. The
// tempo BPM and the tuner's reference pitch use it.
class NumberField : public QDoubleSpinBox
{
    Q_OBJECT
public:
    struct Spec
    {
        double minimum;
        double maximum;
        int decimals; // values round to this many places
        double step; // one arrow click or scroll notch
        QString unit; // after the number ("BPM", "Hz")
    };

    explicit NumberField(const Spec& spec, QWidget* parent = nullptr);

    void set_value(double new_value) {
        setValue(new_value); // clamps + rounds; emits on change
    }

    void stepBy(int steps) override;
    // Whole values without decimals: "120", but "120.5".
    QString textFromValue(double v) const override;

signals:
    void value_changed(double value);

protected:
    bool event(QEvent* ev) override;
};

} // namespace namx
