#pragma once

#include <cstdint>
#include <functional>

#include <QElapsedTimer>
#include <QFrame>
#include <QVector>

#include "Common.h"

class QLineEdit;
class QMenu;
class QToolButton;

namespace npl
{

class KnobWidget;
class Metronome;

/// BPM readout you can drag (vertical: 1 BPM per 4 px, Shift = 0.1), scroll
/// (±1, Shift ±0.1) or click to type into.
class BpmField : public QWidget
{
    Q_OBJECT
public:
    explicit BpmField(QWidget* parent = nullptr);

    double value() const {
        return bpm;
    }
    void set_value(double new_bpm); // clamps + rounds to 0.1; emits on change

    QSize sizeHint() const override {
        return {86, 30};
    }

signals:
    void value_changed(double bpm);

protected:
    void paintEvent(QPaintEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void wheelEvent(QWheelEvent* ev) override;

private:
    void begin_edit();
    void end_edit(bool commit);

    double bpm = 120.0;
    QLineEdit* edit = nullptr;
    bool dragging = false;
    bool pressed = false;
    int press_y = 0;
    double press_bpm = 0.0;
};

/// One column per beat: height = accent (strong / normal / soft / mute
/// outline), the current beat lit. Clicking a beat cycles its accent.
class BeatLeds : public QWidget
{
    Q_OBJECT
public:
    explicit BeatLeds(QWidget* parent = nullptr);

    void set_pattern(int new_beats, uint32_t new_accents);
    /// Current beat (-1 = stopped); `countIn` lights it amber.
    void set_beat(int new_beat, bool new_count_in);

    QSize sizeHint() const override;

signals:
    void accents_changed(uint32_t accents);

protected:
    void paintEvent(QPaintEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;

private:
    static constexpr int W = 12, GAP = 4, H = 26;
    int beats = 4;
    uint32_t accents = 0;
    int beat = -1;
    bool count_in = false;
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

    double get_bpm() const;
    void set_bpm(double new_bpm); // MIDI clock follow
    void toggle_play();
    void tap_tempo() {
        tap();
    }
    /// Beat LEDs follow the audio thread — call at display rate.
    void poll();

    QWidget* action_widget(const QString& id) const;
    void set_learn_menu_hook(std::function<void(const QString& action_id, QMenu* menu)> hook) {
        learn_hook = std::move(hook);
    }

signals:
    void bpm_changed(double bpm);

private:
    void tap();
    void set_meter(int beats, int unit, bool reset_accents);
    void update_labels();
    QToolButton* menu_button(const QString& tip);

    Metronome& metro;
    BpmField* bpm = nullptr;
    QToolButton* tap_button = nullptr;
    QToolButton* play = nullptr;
    BeatLeds* leds = nullptr;
    QToolButton* meter_btn = nullptr;
    QToolButton* subdiv_btn = nullptr;
    QToolButton* sound_btn = nullptr;
    QToolButton* count_in_btn = nullptr;
    KnobWidget* volume = nullptr;
    KnobWidget* pan = nullptr;
    QElapsedTimer tap_clock;
    QVector<qint64> taps; // ms timestamps of the recent taps
    std::function<void(const QString&, QMenu*)> learn_hook;
    void learn_menu_for(QWidget* w, const QString& action_id);
};

} // namespace npl
