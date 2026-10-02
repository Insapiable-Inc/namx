#pragma once

#include <cstdint>
#include <functional>

#include <QElapsedTimer>
#include <QFrame>
#include <QVector>

#include "Common.h"
#include "NumberField.h"

class QMenu;
class QToolButton;

namespace npl
{

class KnobWidget;
class Metronome;

// One column per beat: height = accent (strong / normal / soft / mute
// outline), the current beat lit. Clicking a beat cycles its accent.
class BeatLeds : public QWidget
{
    Q_OBJECT
public:
    explicit BeatLeds(QWidget* parent = nullptr);

    void set_pattern(int new_beats, uint32_t new_accents);
    // Current beat (-1 = stopped); `countIn` lights it amber.
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

// Global tempo + metronome strip under the preset bar.
//
// The tempo is a session setting (QSettings tempo/bpm), not part of presets
// — a preset stores note values and follows whatever tempo you play at. The
// metronome's settings live in QSettings metronome/* (excluded from presets
// too) and it keeps running across preset loads. Space starts/stops it.
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
    // Beat LEDs follow the audio thread — call at display rate.
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
    NumberField* bpm = nullptr;
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
    void show_learn_menu(const QString& action_id, const QPoint& global_pos);

    void on_bpm_field_changed(double new_bpm);
    void on_play_toggled(bool on);
    void on_accents_edited(uint32_t accents);
    void populate_meter_menu(); // time signatures, rebuilt each time it opens
    void ask_custom_meter();
    void choose_subdivision(int subdivision); // 1 = beats … 4 = sixteenths
    void choose_sound(int sound);
    void choose_count_in(int bars);
    void choose_silent_after_count_in(bool on);
    void on_volume_changed(double db);
    void on_pan_changed(double pct);
};

} // namespace npl
