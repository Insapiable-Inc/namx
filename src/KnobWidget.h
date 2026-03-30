#pragma once

#include <QWidget>
#include <QString>

class QDial;
class QLabel;

/// A knob widget built around Qt's QDial.
/// Wraps QDial with a floating-point value range, name label, and value display.
/// Emits valueChanged(double) when the user adjusts the dial.
class KnobWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(double value READ value WRITE setValue NOTIFY valueChanged)

public:
    explicit KnobWidget(const QString& label, double minVal, double maxVal,
                        double defaultVal, const QString& unit = "",
                        QWidget* parent = nullptr);

    double value() const;

    QSize sizeHint()        const override { return {80, 90}; }
    QSize minimumSizeHint() const override { return {64, 76}; }

public slots:
    void setValue(double v);
    void resetToDefault();

signals:
    void valueChanged(double newValue);

private slots:
    void onDialChanged(int dialVal);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QDial*  mDial;
    QLabel* mLabelValue;
    double  mMin;
    double  mMax;
    double  mDefault;
    QString mUnit;

    // Internal dial range [0, kSteps]
    static constexpr int kSteps = 10000;

    double dialToValue(int d) const;
    int    valueToDial(double v) const;
};
