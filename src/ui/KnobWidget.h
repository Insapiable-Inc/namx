#pragma once

#include <functional>
#include <optional>

#include <QColor>
#include <QString>
#include <QWidget>

#include "Common.h"

class QContextMenuEvent;
class QLineEdit;
class QLabel;
class QMenu;

namespace namx
{

class VerticalDial;

// Knob widget wrapping a VerticalDial (responds to vertical mouse drag).
// Emits valueChanged(double) and uses QPalette::Highlight for the value label.
class KnobWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(double value READ value WRITE set_value NOTIFY value_changed)

public:
    // Compact: dial left, name over value right (effect rows, ~46px tall).
    // Stacked: name over a large dial over the value, all centred — the
    // prominent form for the Input/Output gain columns.
    // Panel: name on top, then a value field (click to type) left of a large dial —
    // the node-graph parameter panel (ui/node-graph).
    enum class Form
    {
        COMPACT,
        STACKED,
        PANEL
    };

    explicit KnobWidget(const QString& new_label, double min_val, double max_val,
                        double default_val, const QString& new_unit = "", Form form = Form::COMPACT,
                        QWidget* parent = nullptr);

    double value() const;
    double get_default_value() const {
        return default_value;
    }
    double minimum() const {
        return min;
    }
    double maximum() const {
        return max;
    }
    QString get_label() const {
        return label;
    }
    const QString& get_unit() const {
        return unit;
    }
    double get_snap() const {
        return snap;
    }

    // Extra entries for every knob's context menu (MIDI Learn). Set once by
    // the owner of the parameter registry.
    static void set_menu_hook(std::function<void(KnobWidget*, QMenu*)> hook);

    // Explicit accent color for the value arc, indicator tick, and value
    // label (the knob body stays neutral). Pushed by Theme::tagCard — the
    // app stylesheet disables parent→child palette propagation, so palette
    // Highlight can NOT be used for this.
    void set_accent(const QColor& new_accent);
    QColor get_accent() const {
        return accent;
    }

    // When true, the value label shows a rounded integer.
    void set_integer_display(bool on) {
        int_display = on;
        update_label();
    }

    // Discrete knob: value() and valueChanged snap to multiples of `step`
    // (e.g. 1.0 for a list index); 0 = continuous. A discrete knob with up to
    // MAX_DRAWN_POSITIONS positions also looks stepped: a dot per position on its ring.
    void set_snap(double step);
    // Custom value label (e.g. note names for a sync knob); replaces the
    // number + unit text. Pass {} to restore the default.
    void set_value_formatter(std::function<QString(double)> f) {
        formatter = std::move(f);
        update_label();
    }

    // PANEL form: how a typed value is read (the formatter's inverse); the default takes
    // the first number, "k" multiplying by 1000 on an Hz knob. nullopt = not a value.
    void set_value_parser(std::function<std::optional<double>(const QString&)> f) {
        parser = std::move(f);
    }

    // Hides the name line; for a knob whose value label names itself ("Type: Plate").
    void set_name_visible(bool on);

    // Logarithmic dial (frequencies, times, rates): equal travel = equal
    // ratio, so 100 Hz–1 kHz gets as much of the sweep as 1–10 kHz. Values
    // stored in presets don't change — only how the dial maps to them.
    // Requires min > 0. The wheel then steps 1 % of the travel.
    void set_log_taper(bool on);

    // Set the scroll-wheel increment in value units (e.g. 0.1 for 0.1 dB).
    // Pass 0 to use the default (1% of range).
    void set_scroll_step(double step) {
        scroll_step = step;
    }

public slots:
    void set_value(double v);
    void reset_to_default();

signals:
    void value_changed(double new_value);

private slots:
    void on_dial_changed(int dial_val);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    VerticalDial* dial;
    QLabel* name_label;
    QLabel* label_value;
    QLineEdit* field = nullptr; // PANEL form: the value, editable
    std::function<std::optional<double>(const QString&)> parser;
    void commit_field();
    QColor accent;
    double min, max, default_value;
    QString unit;
    QString label;
    bool int_display{false};
    double scroll_step{0.0};
    double snap{0.0};
    bool log_taper{false};
    double last_emitted{0.0}; // snapped knobs emit only on a step change
    std::function<QString(double)> formatter;

    static constexpr int MAX_DRAWN_POSITIONS = 32;
    static constexpr int STEPS = 100000; // fine enough that log-taper round trips stay exact at 0.1

    double dial_to_value(int d) const;
    int value_to_dial(double v) const;
    void update_label();
};

} // namespace namx
