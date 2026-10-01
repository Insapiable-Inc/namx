#pragma once

#include <QElapsedTimer>
#include <QFrame>
#include <QVector>

class QLineEdit;
class QToolButton;

/// BPM readout you can drag (vertical: 1 BPM per 4 px, Shift = 0.1), scroll
/// (±1, Shift ±0.1) or click to type into.
class BpmField : public QWidget
{
    Q_OBJECT
public:
    explicit BpmField(QWidget* parent = nullptr);

    double value() const { return mBpm; }
    void   setValue(double bpm); // clamps + rounds to 0.1; emits on change

    QSize sizeHint() const override { return {86, 30}; }

signals:
    void valueChanged(double bpm);

protected:
    void paintEvent(QPaintEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void wheelEvent(QWheelEvent* ev) override;

private:
    void beginEdit();
    void endEdit(bool commit);

    double     mBpm = 120.0;
    QLineEdit* mEdit = nullptr;
    bool       mDragging = false;
    bool       mPressed  = false;
    int        mPressY   = 0;
    double     mPressBpm = 0.0;
};

/// Global tempo strip: BPM field + Tap. The tempo is a session setting
/// (QSettings tempo/bpm), not part of presets — a preset stores note values
/// and follows whatever tempo you play at.
class TempoBar : public QFrame
{
    Q_OBJECT
public:
    explicit TempoBar(QWidget* parent = nullptr);

    double bpm() const;

signals:
    void bpmChanged(double bpm);

private:
    void tap();

    BpmField*      mBpm = nullptr;
    QToolButton*   mTap = nullptr;
    QElapsedTimer  mTapClock;
    QVector<qint64> mTaps; // ms timestamps of the recent taps
};
