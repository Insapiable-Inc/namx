#pragma once

#include <optional>

#include <QWidget>

#include "Common.h"

class QLineEdit;

namespace namx
{

// Numeric readout in a well: "value unit". Drag vertically, scroll
// or click to type; Shift makes drag and scroll ten times finer. The tempo BPM
// and the tuner's reference pitch use it, so both look and behave the same.
class NumberField : public QWidget
{
    Q_OBJECT
public:
    struct Spec
    {
        double minimum;
        double maximum;
        int decimals; // values round to this many places
        double step; // one scroll notch; four pixels of drag
        QString unit; // dim, right of the number ("BPM", "Hz")
        std::optional<double> reset_value; // double-click restores it (like a knob's default)
    };

    explicit NumberField(const Spec& new_spec, QWidget* parent = nullptr);

    double value() const {
        return current;
    }
    double minimum() const {
        return spec.minimum;
    }
    double maximum() const {
        return spec.maximum;
    }
    void set_value(double new_value); // clamps + rounds; emits on change

    QSize sizeHint() const override;

signals:
    void value_changed(double value);

protected:
    void paintEvent(QPaintEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void wheelEvent(QWheelEvent* ev) override;
    bool eventFilter(QObject* watched, QEvent* ev) override; // Escape cancels the edit

private:
    QString number_text() const;
    void begin_edit();
    void end_edit(bool commit);

    Spec spec;
    double current;
    QLineEdit* edit = nullptr;
    bool dragging = false;
    bool pressed = false;
    int press_y = 0;
    double press_value = 0.0;
};

} // namespace namx
