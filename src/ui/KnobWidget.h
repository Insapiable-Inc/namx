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
    explicit KnobWidget(const QString& label, double minVal, double maxVal,
                        double defaultVal, const QString& unit = "",
                        QWidget* parent = nullptr);

    double value() const;
    double defaultValue() const { return mDefault; }

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

    QSize sizeHint()        const override { return {94, 116}; }
    QSize minimumSizeHint() const override { return {80, 100}; }

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
    void paintEvent(QPaintEvent* event) override;

private:
    VerticalDial* mDial;
    QLabel*       mLabelValue;
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
