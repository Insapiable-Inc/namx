#pragma once

#include <QElapsedTimer>
#include <QFrame>
#include <QVector>

#include <cstdint>

class KnobWidget;
class Metronome;
class QLineEdit;
class QMenu;
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

/// One column per beat: height = accent (strong / normal / soft / mute
/// outline), the current beat lit. Clicking a beat cycles its accent.
class BeatLeds : public QWidget
{
    Q_OBJECT
public:
    explicit BeatLeds(QWidget* parent = nullptr);

    void setPattern(int beats, uint32_t accents);
    /// Current beat (-1 = stopped); `countIn` lights it amber.
    void setBeat(int beat, bool countIn);

    QSize sizeHint() const override;

signals:
    void accentsChanged(uint32_t accents);

protected:
    void paintEvent(QPaintEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;

private:
    static constexpr int kW = 12, kGap = 4, kH = 26;
    int      mBeats   = 4;
    uint32_t mAccents = 0;
    int      mBeat    = -1;
    bool     mCountIn = false;
};

/// Global tempo + metronome strip under the preset bar.
///
/// The tempo is a session setting (QSettings tempo/bpm), not part of presets
/// — a preset stores note values and follows whatever tempo you play at. The
/// metronome's settings live in QSettings metronome/* (excluded from presets
/// too) and it keeps running across preset loads. Space starts/stops it.
class TempoBar : public QFrame
{
    Q_OBJECT
public:
    explicit TempoBar(Metronome& metronome, QWidget* parent = nullptr);

    double bpm() const;
    /// Beat LEDs follow the audio thread — call at display rate.
    void poll();

signals:
    void bpmChanged(double bpm);

private:
    void tap();
    void setMeter(int beats, int unit, bool resetAccents);
    void updateLabels();
    QToolButton* menuButton(const QString& tip);

    Metronome&     mMetro;
    BpmField*      mBpm = nullptr;
    QToolButton*   mTap = nullptr;
    QToolButton*   mPlay = nullptr;
    BeatLeds*      mLeds = nullptr;
    QToolButton*   mMeterBtn = nullptr;
    QToolButton*   mSubdivBtn = nullptr;
    QToolButton*   mSoundBtn = nullptr;
    QToolButton*   mCountInBtn = nullptr;
    KnobWidget*    mVolume = nullptr;
    KnobWidget*    mPan = nullptr;
    QElapsedTimer  mTapClock;
    QVector<qint64> mTaps; // ms timestamps of the recent taps
};
