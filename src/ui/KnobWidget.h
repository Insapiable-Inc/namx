#pragma once

#include <functional>

#include <QColor>
#include <QString>
#include <QWidget>

class QContextMenuEvent;
class QLabel;
class QMenu;
class VerticalDial;

/// Knob widget wrapping a VerticalDial (responds to vertical mouse drag).
/// Emits valueChanged(double) and uses QPalette::Highlight for the value label.
class KnobWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(double value READ value WRITE set_value NOTIFY value_changed)

public:
    /// Compact: dial left, name over value right (effect rows, ~46px tall).
    /// Stacked: name over a large dial over the value, all centred — the
    /// prominent form for the Input/Output gain columns.
    enum class Form
    {
        COMPACT,
        STACKED
    };

    explicit KnobWidget(const QString& label, double min_val, double max_val, double default_val,
                        const QString& unit = "", Form form = Form::COMPACT,
                        QWidget* parent = nullptr);

    double value() const;
    double default_value() const {
        return m_default;
    }
    double minimum() const {
        return m_min;
    }
    double maximum() const {
        return m_max;
    }
    QString label() const {
        return m_label;
    }

    /// Extra entries for every knob's context menu (MIDI Learn). Set once by
    /// the owner of the parameter registry.
    static void set_menu_hook(std::function<void(KnobWidget*, QMenu*)> hook);

    /// Explicit accent color for the value arc, indicator tick, and value
    /// label (the knob body stays neutral). Pushed by Theme::tagCard — the
    /// app stylesheet disables parent→child palette propagation, so palette
    /// Highlight can NOT be used for this.
    void set_accent(const QColor& accent);

    /// When true, the value label shows a rounded integer.
    void set_integer_display(bool on) {
        m_int_display = on;
        update_label();
    }

    /// Discrete knob: value() and valueChanged snap to multiples of `step`
    /// (e.g. 1.0 for a list index); 0 = continuous.
    void set_snap(double step) {
        m_snap = step;
        update_label();
    }
    /// Custom value label (e.g. note names for a sync knob); replaces the
    /// number + unit text. Pass {} to restore the default.
    void set_value_formatter(std::function<QString(double)> f) {
        m_formatter = std::move(f);
        update_label();
    }

    /// Logarithmic dial (frequencies, times, rates): equal travel = equal
    /// ratio, so 100 Hz–1 kHz gets as much of the sweep as 1–10 kHz. Values
    /// stored in presets don't change — only how the dial maps to them.
    /// Requires min > 0. The wheel then steps 1 % of the travel.
    void set_log_taper(bool on);

    /// Set the scroll-wheel increment in value units (e.g. 0.1 for 0.1 dB).
    /// Pass 0 to use the default (1% of range).
    void set_scroll_step(double step) {
        m_scroll_step = step;
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
    VerticalDial* m_dial;
    QLabel* m_label_value;
    QColor m_accent;
    double m_min, m_max, m_default;
    QString m_unit;
    QString m_label;
    bool m_int_display{false};
    double m_scroll_step{0.0};
    double m_snap{0.0};
    bool m_log_taper{false};
    double m_last_emitted{0.0}; // snapped knobs emit only on a step change
    std::function<QString(double)> m_formatter;

    static constexpr int K_STEPS =
        100000; // fine enough that log-taper round trips stay exact at 0.1

    double dial_to_value(int d) const;
    int value_to_dial(double v) const;
    void update_label();
};
