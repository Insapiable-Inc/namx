#pragma once

#include <functional>
#include <span>
#include <vector>

#include <QColor>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "Common.h"

class QContextMenuEvent;
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
    enum class Form
    {
        COMPACT,
        STACKED
    };

    explicit KnobWidget(const QString& new_label, double min_val, double max_val,
                        double default_val, const QString& new_unit = "",
                        Form new_form = Form::COMPACT, QWidget* parent = nullptr);

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

    // Hides the name line; for a knob whose value label names itself ("Type: Plate").
    void set_name_visible(bool on);
    // Renames the knob (e.g. a type that calls the control by its own name). True when the
    // name is new to it: the owner refits the text column (text_width_needed counts every
    // name the knob has had).
    bool set_label(const QString& new_label);
    // A name that follows something else as it moves (an Equalizer band is named by its
    // frequency): shown, not remembered. Give set_label the longest such names once, so the
    // text column is measured for them.
    void set_live_label(const QString& text);

    // Compact knobs line up in columns: each tile's text column has one width, set by
    // the owner from what every knob needs (its name, its widest value in the font in
    // use) and never below TEXT_COLUMN_WIDTH (the longest at 9 pt, "Semitones", and a little
    // air: six knobs on a row then fit a 1920-wide window with the library open).
    static constexpr int TEXT_COLUMN_WIDTH = 68;
    bool is_compact() const {
        return form == Form::COMPACT;
    }
    int text_width_needed() const;
    void set_text_column_width(int width);

    // Logarithmic dial (frequencies, times, rates): equal travel = equal
    // ratio, so 100 Hz–1 kHz gets as much of the sweep as 1–10 kHz. Values
    // stored in presets don't change — only how the dial maps to them.
    // Requires min > 0. The wheel then steps 1 % of the travel.
    void set_log_taper(bool on);

    // A measured curve, e.g. a pedal's own knob: the value at points along the travel
    // (0 = fully counter-clockwise, 1 = fully clockwise), log-interpolated between them as
    // set_log_taper is between the ends. The first point is (0, min), the last (1, max), travel
    // and value rising, the values positive — but the first may be 0 (a rate knob's Stop): the
    // dial then runs straight from 0 to the second point. Values stored in presets don't
    // change. With a curve the wheel steps 1 % of the travel; an empty one makes the dial
    // linear again.
    struct TaperPoint
    {
        double travel;
        double value;
    };
    void set_taper(std::span<const TaperPoint> points);

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
    Form form;
    QColor accent;
    double min, max, default_value;
    QString unit;
    QString label;
    QStringList other_labels; // names set_label gave it, measured by text_width_needed
    bool label_is_live = false; // the name in use came from set_live_label
    bool int_display{false};
    double scroll_step{0.0};
    double snap{0.0};
    std::vector<TaperPoint> taper; // empty = linear
    double last_emitted{0.0}; // snapped knobs emit only on a step change
    std::function<QString(double)> formatter;

    static constexpr int MAX_DRAWN_POSITIONS = 32;
    static constexpr int STEPS = 100000; // fine enough that log-taper round trips stay exact at 0.1

    double dial_to_value(int d) const;
    int value_to_dial(double v) const;
    QString text_for(double v) const;
    void update_label();
};

} // namespace namx
