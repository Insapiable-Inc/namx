#include "TempoBar.h"

#include <algorithm>
#include <cmath>

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QShortcut>
#include <QToolButton>

#include "Common.h"
#include "KnobWidget.h"
#include "Metronome.h"
#include "TempoSync.h"
#include "Theme.h"

namespace namx
{

BeatLeds::BeatLeds(QWidget* parent)
    : QWidget(parent) {
    setCursor(Qt::PointingHandCursor);
    setToolTip("Beats of the bar. Click a beat to cycle its accent: strong → normal → soft → mute");
}

QSize BeatLeds::sizeHint() const {
    return {beats * W + (beats - 1) * GAP + 2, H};
}

void BeatLeds::set_pattern(int new_beats, uint32_t new_accents) {
    beats = std::clamp(new_beats, 1, Metronome::MAX_BEATS);
    accents = new_accents;
    setFixedSize(sizeHint());
    update();
}

void BeatLeds::set_beat(int new_beat, bool new_count_in) {
    if (new_beat == beat && new_count_in == count_in) {
        return;
    }
    beat = new_beat;
    count_in = new_count_in;
    update();
}

void BeatLeds::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    for (int b = 0; b < beats; ++b) {
        const Metronome::Accent a = Metronome::accent_of(accents, b);
        const qreal x = 1 + b * (W + GAP);
        const qreal h = a == Metronome::STRONG   ? H - 2
                        : a == Metronome::NORMAL ? (H - 2) * 0.66
                        : a == Metronome::SOFT   ? (H - 2) * 0.36
                                                 : (H - 2) * 0.36;
        const QRectF r(x, H - 1 - h, W, h);
        const bool lit = b == beat;
        QColor c =
            lit ? (count_in ? Theme::meter_mid() : Theme::generic_accent()) : Theme::track_hover();
        if (a == Metronome::MUTE) {
            p.setPen(QPen(lit ? c : Theme::track_hover(), 1.2));
            p.setBrush(Qt::NoBrush);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(c);
        }
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 2.5, 2.5);
    }
}

void BeatLeds::mousePressEvent(QMouseEvent* ev) {
    const int b = static_cast<int>(ev->position().x()) / (W + GAP);
    if (b < 0 || b >= beats) {
        return;
    }
    // strong → normal → soft → mute → strong
    const Metronome::Accent a = Metronome::accent_of(accents, b);
    const Metronome::Accent next =
        a == Metronome::MUTE ? Metronome::STRONG : Metronome::Accent(a - 1);
    accents = Metronome::with_accent(accents, b, next);
    update();
    emit accents_changed(accents);
}

static const char* const SOUND_NAMES[] = {"Click", "Wood", "Beep"};
static const char* const SUBDIV_NAMES[] = {"Beats", "Eighths", "Triplets", "Sixteenths"};
static const char* const SUBDIV_SHORT[] = {"♩", "♪♪", "3", "♬"};
struct MeterPreset
{
    int beats, unit;
};
static const MeterPreset METERS[] = {{2, 4}, {3, 4}, {4, 4}, {5, 4}, {6, 4}, {7, 4},
                                     {3, 8}, {5, 8}, {6, 8}, {7, 8}, {9, 8}, {12, 8}};

QToolButton* TempoBar::menu_button(const QString& tip) {
    auto* b = new QToolButton(this);
    b->setObjectName("menuTool");
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus);
    b->setPopupMode(QToolButton::InstantPopup);
    b->setMenu(new QMenu(b));
    return b;
}

// Pan knob readout: "Center", "40 L", "25 R".
static QString format_pan(double v) {
    const int p = static_cast<int>(std::lround(v));
    return p == 0 ? QString("Center") : QString("%1 %2").arg(std::abs(p)).arg(p < 0 ? "L" : "R");
}

TempoBar::TempoBar(Metronome& metronome, QWidget* parent)
    : QFrame(parent)
    , metro(metronome) {
    setObjectName("tempoBar");
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(8, 2, 8, 2);
    lay->setSpacing(6);
    QSettings s;

    auto* caption = new QLabel("Tempo", this);
    caption->setObjectName("presetCaption");
    lay->addWidget(caption);

    bpm = new NumberField({.minimum = TempoSync::MIN_BPM,
                           .maximum = TempoSync::MAX_BPM,
                           .decimals = 1,
                           .step = 1.0,
                           .unit = "BPM"},
                          this);
    bpm->setToolTip("Tempo (quarter notes per minute): drag up/down or scroll to change "
                    "(Shift = fine), click to type");
    bpm->set_value(s.value("tempo/bpm", TempoSync::DEFAULT_BPM).toDouble());
    metro.set_bpm(bpm->value());
    connect(bpm, &NumberField::value_changed, this, &TempoBar::on_bpm_field_changed);
    lay->addWidget(bpm);

    tap_button = new QToolButton(this);
    tap_button->setObjectName("presetTool");
    tap_button->setText("Tap");
    tap_button->setToolTip("Tap the tempo: four taps or more average out");
    tap_button->setFocusPolicy(Qt::NoFocus);
    connect(tap_button, &QToolButton::pressed, this, &TempoBar::tap); // on press: tighter timing
    learn_menu_for(tap_button, "action:tap");
    lay->addWidget(tap_button);
    lay->addSpacing(18);

    // Metronome
    auto* metro_caption = new QLabel("Metronome", this);
    metro_caption->setObjectName("presetCaption");
    lay->addWidget(metro_caption);

    play = new QToolButton(this);
    play->setObjectName("metroPlay");
    play->setCheckable(true);
    play->setText("▶");
    play->setToolTip("Start / stop the metronome (Space)");
    play->setFocusPolicy(Qt::NoFocus);
    connect(play, &QToolButton::toggled, this, &TempoBar::on_play_toggled);
    auto* space = new QShortcut(QKeySequence(Qt::Key_Space), this);
    space->setContext(Qt::WindowShortcut);
    space->setWhatsThis("Start or stop the metronome");
    connect(space, &QShortcut::activated, play, &QToolButton::toggle);
    learn_menu_for(play, "action:metronome");
    lay->addWidget(play);

    leds = new BeatLeds(this);
    connect(leds, &BeatLeds::accents_changed, this, &TempoBar::on_accents_edited);
    lay->addWidget(leds, 0, Qt::AlignVCenter);

    // Meter.
    meter_btn = menu_button("Time signature");
    connect(meter_btn->menu(), &QMenu::aboutToShow, this, &TempoBar::populate_meter_menu);
    lay->addWidget(meter_btn);

    // Subdivision.
    subdiv_btn = menu_button("Subdivision: extra clicks between beats");
    for (int i = 0; i < 4; ++i) {
        connect(subdiv_btn->menu()->addAction(SUBDIV_NAMES[i]), &QAction::triggered, this,
                [this, i] {
                    choose_subdivision(i + 1);
                });
    }
    lay->addWidget(subdiv_btn);

    // Sound.
    sound_btn = menu_button("Click sound");
    for (int i = 0; i < Metronome::SOUND_COUNT; ++i) {
        connect(sound_btn->menu()->addAction(SOUND_NAMES[i]), &QAction::triggered, this, [this, i] {
            choose_sound(i);
        });
    }
    lay->addWidget(sound_btn);

    // Count-in.
    count_in_btn = menu_button("Count-in bars when starting");
    {
        QMenu* m = count_in_btn->menu();
        for (int bars : {0, 1, 2}) {
            connect(
                m->addAction(bars == 0
                                 ? QString("No count-in")
                                 : QString("Count in %1 bar%2").arg(bars).arg(bars > 1 ? "s" : "")),
                &QAction::triggered, this, [this, bars] {
                    choose_count_in(bars);
                });
        }
        m->addSeparator();
        QAction* silent = m->addAction("Silent after count-in (lights only)");
        silent->setCheckable(true);
        silent->setChecked(s.value("metronome/silentAfter", false).toBool());
        connect(silent, &QAction::toggled, this, &TempoBar::choose_silent_after_count_in);
        metro.set_silent_after_count_in(silent->isChecked());
    }
    lay->addWidget(count_in_btn);

    // Level + pan.
    volume = new KnobWidget("Level", -40.0, 6.0, -6.0, "dB");
    volume->set_scroll_step(1.0);
    volume->set_accent(Theme::generic_accent());
    volume->set_value(s.value("metronome/volume", -6.0).toDouble());
    metro.set_volume_db(static_cast<float>(volume->value()));
    connect(volume, &KnobWidget::value_changed, this, &TempoBar::on_volume_changed);
    pan = new KnobWidget("Pan", -100.0, 100.0, 0.0, "%");
    pan->set_scroll_step(10.0);
    pan->set_accent(Theme::generic_accent());
    pan->set_value_formatter(&format_pan);
    pan->set_value(s.value("metronome/pan", 0.0).toDouble());
    metro.set_pan(static_cast<float>(pan->value() / 100.0));
    connect(pan, &KnobWidget::value_changed, this, &TempoBar::on_pan_changed);
    lay->addWidget(volume);
    lay->addWidget(pan);
    lay->addStretch(1);

    // Restore the rest.
    metro.set_subdivision(s.value("metronome/subdivision", 1).toInt());
    metro.set_sound(s.value("metronome/sound", 0).toInt());
    metro.set_count_in_bars(s.value("metronome/countIn", 0).toInt());
    const int beats = s.value("metronome/beats", 4).toInt();
    const int unit = s.value("metronome/unit", 4).toInt();
    set_meter(beats, unit, !s.contains("metronome/accents"));
    if (s.contains("metronome/accents")) {
        const uint32_t acc = s.value("metronome/accents").toUInt();
        metro.set_accents(acc);
        leds->set_pattern(metro.get_beats(), acc);
    }
    update_labels();
}

double TempoBar::get_bpm() const {
    return bpm->value();
}

void TempoBar::set_bpm(double new_bpm) {
    bpm->set_value(new_bpm);
}

void TempoBar::toggle_play() {
    play->toggle();
}

QWidget* TempoBar::action_widget(const QString& id) const {
    if (id == "action:metronome") {
        return play;
    }
    if (id == "action:tap") {
        return tap_button;
    }
    return nullptr;
}

void TempoBar::learn_menu_for(QWidget* w, const QString& action_id) {
    w->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(w, &QWidget::customContextMenuRequested, this, [this, w, action_id](const QPoint& p) {
        show_learn_menu(action_id, w->mapToGlobal(p));
    });
}

void TempoBar::show_learn_menu(const QString& action_id, const QPoint& global_pos) {
    if (!learn_hook) {
        return;
    }
    QMenu menu(this);
    learn_hook(action_id, &menu);
    if (!menu.isEmpty()) {
        menu.exec(global_pos);
    }
}

void TempoBar::poll() {
    leds->set_beat(metro.is_running() ? metro.current_beat() : -1, metro.in_count_in());
}

void TempoBar::set_meter(int beats, int unit, bool reset_accents) {
    metro.set_meter(beats, unit);
    QSettings s;
    s.setValue("metronome/beats", metro.get_beats());
    s.setValue("metronome/unit", metro.get_unit());
    if (reset_accents) {
        const uint32_t acc = Metronome::default_accents(metro.get_beats(), metro.get_unit());
        metro.set_accents(acc);
        s.setValue("metronome/accents", acc);
    }
    leds->set_pattern(metro.get_beats(), metro.get_accents());
    update_labels();
}

void TempoBar::update_labels() {
    QSettings s;
    meter_btn->setText(QString("%1/%2").arg(metro.get_beats()).arg(metro.get_unit()));
    const int sub = std::clamp(s.value("metronome/subdivision", 1).toInt(), 1, 4);
    subdiv_btn->setText(SUBDIV_SHORT[sub - 1]);
    subdiv_btn->setToolTip(QString("Subdivision: %1").arg(SUBDIV_NAMES[sub - 1]));
    // An unknown sound (e.g. the removed Cowbell) plays Click, as Metronome::set_sound does.
    const int sound = s.value("metronome/sound", 0).toInt();
    sound_btn->setText(SOUND_NAMES[sound >= 0 && sound < Metronome::SOUND_COUNT ? sound : 0]);
    const int ci = s.value("metronome/countIn", 0).toInt();
    const bool silent = s.value("metronome/silentAfter", false).toBool();
    count_in_btn->setText(ci == 0
                              ? QString("No count-in")
                              : QString("Count-in %1%2").arg(ci).arg(silent ? " · silent" : ""));
}

void TempoBar::tap() {
    if (!tap_clock.isValid()) {
        tap_clock.start();
    }
    const qint64 now = tap_clock.elapsed();
    // A pause longer than a 40 BPM beat (plus slack) starts a new measurement.
    if (!taps.isEmpty() && now - taps.last() > 2000) {
        taps.clear();
    }
    taps.append(now);
    if (taps.size() > 5) {
        taps.removeFirst(); // average the last four intervals
    }
    if (taps.size() < 2) {
        return;
    }
    const double avg_ms =
        static_cast<double>(taps.last() - taps.first()) / static_cast<double>(taps.size() - 1);
    if (avg_ms > 0.0) {
        bpm->set_value(60000.0 / avg_ms);
    }
}

void TempoBar::on_bpm_field_changed(double new_bpm) {
    QSettings().setValue("tempo/bpm", new_bpm);
    metro.set_bpm(new_bpm);
    emit bpm_changed(new_bpm);
}

void TempoBar::on_play_toggled(bool on) {
    metro.set_running(on);
    play->setText(on ? "■" : "▶");
}

void TempoBar::on_accents_edited(uint32_t accents) {
    metro.set_accents(accents);
    QSettings().setValue("metronome/accents", accents);
}

void TempoBar::populate_meter_menu() {
    QMenu* m = meter_btn->menu();
    m->clear();
    for (const auto& mp : METERS) {
        QAction* a = m->addAction(QString("%1/%2").arg(mp.beats).arg(mp.unit));
        a->setCheckable(true);
        a->setChecked(mp.beats == metro.get_beats() && mp.unit == metro.get_unit());
        connect(a, &QAction::triggered, this, [this, mp] {
            set_meter(mp.beats, mp.unit, true);
        });
    }
    m->addSeparator();
    connect(m->addAction("Custom…"), &QAction::triggered, this, &TempoBar::ask_custom_meter);
}

void TempoBar::ask_custom_meter() {
    bool ok = false;
    const int beats =
        QInputDialog::getInt(this, "Time Signature", "Beats per bar:", metro.get_beats(), 1,
                             Metronome::MAX_BEATS, 1, &ok);
    if (!ok) {
        return;
    }
    const QString unit =
        QInputDialog::getItem(this, "Time Signature", "Beat unit:", {"2", "4", "8", "16"},
                              static_cast<int>(QStringList{"2", "4", "8", "16"}.indexOf(
                                  QString::number(metro.get_unit()))),
                              false, &ok);
    if (ok) {
        set_meter(beats, unit.toInt(), true);
    }
}

void TempoBar::choose_subdivision(int subdivision) {
    metro.set_subdivision(subdivision);
    QSettings().setValue("metronome/subdivision", subdivision);
    update_labels();
}

void TempoBar::choose_sound(int sound) {
    metro.set_sound(sound);
    QSettings().setValue("metronome/sound", sound);
    update_labels();
}

void TempoBar::choose_count_in(int bars) {
    metro.set_count_in_bars(bars);
    QSettings().setValue("metronome/countIn", bars);
    update_labels();
}

void TempoBar::choose_silent_after_count_in(bool on) {
    metro.set_silent_after_count_in(on);
    QSettings().setValue("metronome/silentAfter", on);
    update_labels();
}

void TempoBar::on_volume_changed(double db) {
    metro.set_volume_db(static_cast<float>(db));
    QSettings().setValue("metronome/volume", db);
}

void TempoBar::on_pan_changed(double pct) {
    metro.set_pan(static_cast<float>(pct / 100.0));
    QSettings().setValue("metronome/pan", pct);
}

} // namespace namx
