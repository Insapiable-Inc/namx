#include "NumberField.h"

#include <cmath>

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>

namespace namx
{

NumberField::NumberField(const Spec& spec, QWidget* parent)
    : QDoubleSpinBox(parent) {
    setDecimals(spec.decimals);
    setRange(spec.minimum, spec.maximum);
    setSingleStep(spec.step);
    if (!spec.unit.isEmpty()) {
        setSuffix(" " + spec.unit);
    }
    setAccelerated(true);
    // Not a Tab stop: the window would open with focus (and its ring) in the tempo field.
    setFocusPolicy(Qt::ClickFocus);
    // Typing applies on Enter or when the field loses focus, not per keystroke
    // (typing "90" would pass through 9 BPM).
    setKeyboardTracking(false);
    connect(this, &QDoubleSpinBox::valueChanged, this, &NumberField::value_changed);
    // Done typing: hand the keyboard back, so Space starts the metronome again.
    connect(this, &QAbstractSpinBox::editingFinished, this, [this] {
        if (hasFocus()) {
            clearFocus();
        }
    });
}

void NumberField::stepBy(int steps) {
    if (QApplication::keyboardModifiers() & Qt::ShiftModifier) {
        // Fine: a tenth of a step (the field's decimals permitting).
        setValue(value() + steps * singleStep() / 10.0);
        return;
    }
    QDoubleSpinBox::stepBy(steps);
}

QString NumberField::textFromValue(double v) const {
    return locale().toString(v, 'f', std::fmod(v, 1.0) == 0.0 ? 0 : decimals());
}

bool NumberField::event(QEvent* ev) {
    // A number has no spaces: let Space reach the window's metronome shortcut even
    // while the field has focus.
    if (ev->type() == QEvent::ShortcutOverride &&
        static_cast<QKeyEvent*>(ev)->key() == Qt::Key_Space) {
        return false;
    }
    return QDoubleSpinBox::event(ev);
}

} // namespace namx
