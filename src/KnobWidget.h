#pragma once

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

    /// When true, the value label shows a rounded integer.
    void setIntegerDisplay(bool on) { mIntDisplay = on; updateLabel(); }

    QSize sizeHint()        const override { return {72, 88}; }
    QSize minimumSizeHint() const override { return {60, 76}; }

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
    QLabel*       mLabelValue;
    double mMin, mMax, mDefault;
    QString mUnit;
    bool    mIntDisplay{false};

    static constexpr int kSteps = 10000;

    double dialToValue(int d) const;
    int    valueToDial(double v) const;
    void   updateLabel();
};
