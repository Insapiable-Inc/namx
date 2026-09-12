#pragma once

#include <QColor>
#include <QWidget>
#include <QString>

class QContextMenuEvent;
class QLabel;
class VerticalDial;

/// Knob widget wrapping a VerticalDial (responds to vertical mouse drag).
/// Emits valueChanged(double) and uses QPalette::Highlight for the value label.
class KnobWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(double value READ value WRITE setValue NOTIFY valueChanged)

public:
    /// Compact: dial left, name over value right (effect rows, ~46px tall).
    /// Stacked: name over a large dial over the value, all centred — the
    /// prominent form for the Input/Output gain columns.
    enum class Form { Compact, Stacked };

    explicit KnobWidget(const QString& label, double minVal, double maxVal,
                        double defaultVal, const QString& unit = "",
                        Form form = Form::Compact,
                        QWidget* parent = nullptr);

    double value() const;
    double defaultValue() const { return mDefault; }
    double minimum() const { return mMin; }
    double maximum() const { return mMax; }
    QString label() const { return mLabel; }
    QString unit()  const { return mUnit; }

    /// Explicit accent color for the value arc, indicator tick, and value
    /// label (the knob body stays neutral). Pushed by Theme::tagCard — the
    /// app stylesheet disables parent→child palette propagation, so palette
    /// Highlight can NOT be used for this.
    void setAccent(const QColor& accent);

    /// When true, the value label shows a rounded integer.
    void setIntegerDisplay(bool on) { mIntDisplay = on; updateLabel(); }

    /// Set the scroll-wheel increment in value units (e.g. 0.1 for 0.1 dB).
    /// Pass 0 to use the default (1% of range).
    void setScrollStep(double step) { mScrollStep = step; }
    /// The effective step in value units (scroll step, or 1% of the range).
    double stepSize() const;

    /// Move the value by `direction` (±1) steps, scaled (0.1 = fine, 10 = coarse).
    void stepBy(int direction, double scale = 1.0);

    /// Keyboard-cursor highlight: an accent ring around the dial and the name
    /// in the accent color. Driven by RackNavigator, never by Qt focus.
    void setNavHighlight(bool on);

public slots:
    void setValue(double v);
    void resetToDefault();

signals:
    void valueChanged(double newValue);

private slots:
    void onDialChanged(int dialVal);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    VerticalDial* mDial;
    QLabel*       mLabelName;
    QLabel*       mLabelValue;
    QString mLabel;
    QColor mAccent;
    double mMin, mMax, mDefault;
    QString mUnit;
    bool    mIntDisplay{false};
    double  mScrollStep{0.0};

    static constexpr int kSteps = 10000;

    double dialToValue(int d) const;
    int    valueToDial(double v) const;
    void   updateLabel();
};
