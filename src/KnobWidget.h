#pragma once

#include <QWidget>
#include <QString>

/// A rotary knob widget that renders as a styled arc dial.
/// Emits valueChanged(double) when the user drags or scrolls.
class KnobWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(double value READ value WRITE setValue NOTIFY valueChanged)

public:
    explicit KnobWidget(const QString& label, double minVal, double maxVal,
                        double defaultVal, const QString& unit = "dB",
                        QWidget* parent = nullptr);

    double value() const { return mValue; }

    QSize sizeHint()    const override { return {90, 110}; }
    QSize minimumSizeHint() const override { return {70, 90}; }

public slots:
    void setValue(double v);
    void resetToDefault();

signals:
    void valueChanged(double newValue);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    double mValue;
    double mMin;
    double mMax;
    double mDefault;
    QString mLabel;
    QString mUnit;

    bool   mDragging = false;
    int    mDragStartY = 0;
    double mDragStartValue = 0.0;

    double clamp(double v) const { return std::max(mMin, std::min(mMax, v)); }
    double normalised() const { return (mValue - mMin) / (mMax - mMin); }

    // Visual constants
    static constexpr int   kArcSpanDeg  = 270;  // total arc span in degrees
    static constexpr int   kStartDeg    = 225;   // start angle (from 3 o'clock, CCW)
};
