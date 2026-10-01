#include "TempoBar.h"

#include <algorithm>
#include <cmath>

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QShortcut>
#include <QToolButton>
#include <QWheelEvent>

#include "KnobWidget.h"
#include "Metronome.h"
#include "TempoSync.h"
#include "Theme.h"

// ---------------------------------------------------------------------------
// BpmField
// ---------------------------------------------------------------------------

BpmField::BpmField(QWidget* parent)
    : QWidget(parent) {
    setObjectName("bpmField");
    setFixedSize(sizeHint());
    setCursor(Qt::SizeVerCursor);
    setAttribute(Qt::WA_Hover); // border brightens under the mouse
    setToolTip("Tempo (quarter notes per minute): drag up/down or scroll to change "
               "(Shift = fine), click to type");
}

void BpmField::set_value(double new_bpm) {
    new_bpm =
        std::round(std::clamp(new_bpm, TempoSync::K_MIN_BPM, TempoSync::K_MAX_BPM) * 10.0) / 10.0;
    if (new_bpm == m_bpm) {
        return;
    }
    m_bpm = new_bpm;
    update();
    emit value_changed(m_bpm);
}

void BpmField::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(
        QPen(underMouse() || m_dragging ? Theme::track_hover().lighter(130) : Theme::track(), 1.0));
    p.setBrush(Theme::well());
    p.drawRoundedRect(r, 5, 5);
    if (m_edit) {
        return; // the line edit draws the text while typing
    }

    QFont f = font();
    f.setPointSizeF(12.0);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Theme::generic_accent());
    const QString num = std::fmod(m_bpm, 1.0) == 0.0 ? QString::number(m_bpm, 'f', 0)
                                                     : QString::number(m_bpm, 'f', 1);
    p.drawText(r.adjusted(8, 0, -30, 0), Qt::AlignRight | Qt::AlignVCenter, num);
    f.setPointSizeF(7.5);
    f.setBold(false);
    p.setFont(f);
    p.setPen(Theme::dim_text());
    p.drawText(r.adjusted(0, 0, -6, 0), Qt::AlignRight | Qt::AlignVCenter, "BPM");
}

void BpmField::mousePressEvent(QMouseEvent* ev) {
    if (ev->button() != Qt::LeftButton || m_edit) {
        return QWidget::mousePressEvent(ev);
    }
    m_pressed = true;
    m_dragging = false;
    m_press_y = ev->position().toPoint().y();
    m_press_bpm = m_bpm;
}

void BpmField::mouseMoveEvent(QMouseEvent* ev) {
    if (!m_pressed) {
        return;
    }
    const int dy = m_press_y - ev->position().toPoint().y();
    if (!m_dragging && std::abs(dy) < 3) {
        return;
    }
    m_dragging = true;
    const double per_px = (ev->modifiers() & Qt::ShiftModifier) ? 0.025 : 0.25;
    set_value(m_press_bpm + dy * per_px);
}

void BpmField::mouseReleaseEvent(QMouseEvent* ev) {
    if (ev->button() != Qt::LeftButton || !m_pressed) {
        return;
    }
    m_pressed = false;
    if (!m_dragging) {
        begin_edit(); // a click without a drag: type a value
    }
    m_dragging = false;
    update();
}

void BpmField::wheelEvent(QWheelEvent* ev) {
    const int d = ev->angleDelta().y();
    if (d == 0) {
        return;
    }
    const double step = (ev->modifiers() & Qt::ShiftModifier) ? 0.1 : 1.0;
    set_value(m_bpm + (d > 0 ? step : -step));
}

void BpmField::begin_edit() {
    m_edit = new QLineEdit(QString::number(m_bpm, 'f', std::fmod(m_bpm, 1.0) == 0.0 ? 0 : 1), this);
    m_edit->setObjectName("bpmEdit");
    m_edit->setGeometry(rect().adjusted(2, 2, -2, -2));
    m_edit->setAlignment(Qt::AlignCenter);
    m_edit->selectAll();
    m_edit->show();
    m_edit->setFocus();
    connect(m_edit, &QLineEdit::returnPressed, this, [this] {
        end_edit(true);
    });
    connect(m_edit, &QLineEdit::editingFinished, this, [this] {
        end_edit(true);
    });
    update();
}

void BpmField::end_edit(bool commit) {
    if (!m_edit) {
        return;
    }
    QLineEdit* e = m_edit;
    m_edit = nullptr;
    bool ok = false;
    const double v = e->text().replace(',', '.').toDouble(&ok);
    e->deleteLater();
    if (commit && ok) {
        set_value(v);
    }
    update();
}

// ---------------------------------------------------------------------------
// BeatLeds
// ---------------------------------------------------------------------------

BeatLeds::BeatLeds(QWidget* parent)
    : QWidget(parent) {
    setCursor(Qt::PointingHandCursor);
    setToolTip("Beats of the bar. Click a beat to cycle its accent: strong → normal → soft → mute");
}

QSize BeatLeds::sizeHint() const {
    return {m_beats * K_W + (m_beats - 1) * K_GAP + 2, K_H};
}

void BeatLeds::set_pattern(int new_beats, uint32_t new_accents) {
    m_beats = std::clamp(new_beats, 1, Metronome::K_MAX_BEATS);
    m_accents = new_accents;
    setFixedSize(sizeHint());
    update();
}

void BeatLeds::set_beat(int new_beat, bool new_count_in) {
    if (new_beat == m_beat && new_count_in == m_count_in) {
        return;
    }
    m_beat = new_beat;
    m_count_in = new_count_in;
    update();
}

void BeatLeds::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    for (int b = 0; b < m_beats; ++b) {
        const auto a = Metronome::accent_of(m_accents, b);
        const qreal x = 1 + b * (K_W + K_GAP);
        const qreal h = a == Metronome::STRONG   ? K_H - 2
                        : a == Metronome::NORMAL ? (K_H - 2) * 0.66
                        : a == Metronome::SOFT   ? (K_H - 2) * 0.36
                                                 : (K_H - 2) * 0.36;
        const QRectF r(x, K_H - 1 - h, K_W, h);
        const bool lit = b == m_beat;
        QColor c = lit ? (m_count_in ? Theme::meter_mid() : Theme::generic_accent())
                       : Theme::track_hover();
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
    const int b = static_cast<int>(ev->position().x()) / (K_W + K_GAP);
    if (b < 0 || b >= m_beats) {
        return;
    }
    // strong → normal → soft → mute → strong
    const auto a = Metronome::accent_of(m_accents, b);
    const auto next = a == Metronome::MUTE ? Metronome::STRONG : Metronome::Accent(a - 1);
    m_accents = Metronome::with_accent(m_accents, b, next);
    update();
    emit accents_changed(m_accents);
}

// ---------------------------------------------------------------------------
// TempoBar
// ---------------------------------------------------------------------------

namespace
{
const char* k_sound_names[] = {"Click", "Wood", "Beep", "Cowbell"};
const char* k_subdiv_names[] = {"Beats", "Eighths", "Triplets", "Sixteenths"};
const char* k_subdiv_short[] = {"♩", "♪♪", "3", "♬"};
struct MeterPreset
{
    int beats, unit;
};
const MeterPreset K_METERS[] = {{2, 4}, {3, 4}, {4, 4}, {5, 4}, {6, 4}, {7, 4},
                                {3, 8}, {5, 8}, {6, 8}, {7, 8}, {9, 8}, {12, 8}};
} // namespace

QToolButton* TempoBar::menu_button(const QString& tip) {
    auto* b = new QToolButton(this);
    b->setObjectName("presetTool");
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus);
    b->setPopupMode(QToolButton::InstantPopup);
    b->setMenu(new QMenu(b));
    return b;
}

TempoBar::TempoBar(Metronome& metronome, QWidget* parent)
    : QFrame(parent)
    , m_metro(metronome) {
    setObjectName("tempoBar");
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(8, 2, 8, 2);
    lay->setSpacing(6);
    QSettings s;

    auto* caption = new QLabel("Tempo", this);
    caption->setObjectName("presetCaption");
    lay->addWidget(caption);

    m_bpm = new BpmField(this);
    m_bpm->set_value(s.value("tempo/bpm", TempoSync::K_DEFAULT_BPM).toDouble());
    m_metro.set_bpm(m_bpm->value());
    connect(m_bpm, &BpmField::value_changed, this, [this](double new_bpm) {
        QSettings().setValue("tempo/bpm", new_bpm);
        m_metro.set_bpm(new_bpm);
        emit bpm_changed(new_bpm);
    });
    lay->addWidget(m_bpm);

    m_tap_button = new QToolButton(this);
    m_tap_button->setObjectName("presetTool");
    m_tap_button->setText("Tap");
    m_tap_button->setToolTip("Tap the tempo: four taps or more average out");
    m_tap_button->setFocusPolicy(Qt::NoFocus);
    connect(m_tap_button, &QToolButton::pressed, this, &TempoBar::tap); // on press: tighter timing
    learn_menu_for(m_tap_button, "action:tap");
    lay->addWidget(m_tap_button);
    lay->addSpacing(18);

    // ---- Metronome ----
    auto* metro_caption = new QLabel("Metronome", this);
    metro_caption->setObjectName("presetCaption");
    lay->addWidget(metro_caption);

    m_play = new QToolButton(this);
    m_play->setObjectName("metroPlay");
    m_play->setCheckable(true);
    m_play->setText("▶");
    m_play->setToolTip("Start / stop the metronome (Space)");
    m_play->setFocusPolicy(Qt::NoFocus);
    connect(m_play, &QToolButton::toggled, this, [this](bool on) {
        m_metro.set_running(on);
        m_play->setText(on ? "■" : "▶");
    });
    auto* space = new QShortcut(QKeySequence(Qt::Key_Space), this);
    space->setContext(Qt::WindowShortcut);
    connect(space, &QShortcut::activated, m_play, &QToolButton::toggle);
    learn_menu_for(m_play, "action:metronome");
    lay->addWidget(m_play);

    m_leds = new BeatLeds(this);
    connect(m_leds, &BeatLeds::accents_changed, this, [this](uint32_t a) {
        m_metro.set_accents(a);
        QSettings().setValue("metronome/accents", a);
    });
    lay->addWidget(m_leds, 0, Qt::AlignVCenter);

    // Meter.
    m_meter_btn = menu_button("Time signature");
    connect(m_meter_btn->menu(), &QMenu::aboutToShow, this, [this] {
        QMenu* m = m_meter_btn->menu();
        m->clear();
        for (const auto& mp : K_METERS) {
            QAction* a = m->addAction(QString("%1/%2").arg(mp.beats).arg(mp.unit));
            a->setCheckable(true);
            a->setChecked(mp.beats == m_metro.get_beats() && mp.unit == m_metro.get_unit());
            connect(a, &QAction::triggered, this, [this, mp] {
                set_meter(mp.beats, mp.unit, true);
            });
        }
        m->addSeparator();
        connect(m->addAction("Custom…"), &QAction::triggered, this, [this] {
            bool ok = false;
            const int beats =
                QInputDialog::getInt(this, "Time Signature", "Beats per bar:", m_metro.get_beats(),
                                     1, Metronome::K_MAX_BEATS, 1, &ok);
            if (!ok) {
                return;
            }
            const QString unit =
                QInputDialog::getItem(this, "Time Signature", "Beat unit:", {"2", "4", "8", "16"},
                                      static_cast<int>(QStringList{"2", "4", "8", "16"}.indexOf(
                                          QString::number(m_metro.get_unit()))),
                                      false, &ok);
            if (ok) {
                set_meter(beats, unit.toInt(), true);
            }
        });
    });
    lay->addWidget(m_meter_btn);

    // Subdivision.
    m_subdiv_btn = menu_button("Subdivision: extra clicks between beats");
    for (int i = 0; i < 4; ++i) {
        connect(m_subdiv_btn->menu()->addAction(k_subdiv_names[i]), &QAction::triggered, this,
                [this, i] {
                    m_metro.set_subdivision(i + 1);
                    QSettings().setValue("metronome/subdivision", i + 1);
                    update_labels();
                });
    }
    lay->addWidget(m_subdiv_btn);

    // Sound.
    m_sound_btn = menu_button("Click sound");
    for (int i = 0; i < Metronome::K_SOUND_COUNT; ++i) {
        connect(m_sound_btn->menu()->addAction(k_sound_names[i]), &QAction::triggered, this,
                [this, i] {
                    m_metro.set_sound(i);
                    QSettings().setValue("metronome/sound", i);
                    update_labels();
                });
    }
    lay->addWidget(m_sound_btn);

    // Count-in.
    m_count_in_btn = menu_button("Count-in bars when starting");
    {
        QMenu* m = m_count_in_btn->menu();
        for (int bars : {0, 1, 2}) {
            connect(
                m->addAction(bars == 0
                                 ? QString("No count-in")
                                 : QString("Count in %1 bar%2").arg(bars).arg(bars > 1 ? "s" : "")),
                &QAction::triggered, this, [this, bars] {
                    m_metro.set_count_in_bars(bars);
                    QSettings().setValue("metronome/countIn", bars);
                    update_labels();
                });
        }
        m->addSeparator();
        QAction* silent = m->addAction("Silent after count-in (lights only)");
        silent->setCheckable(true);
        silent->setChecked(s.value("metronome/silentAfter", false).toBool());
        connect(silent, &QAction::toggled, this, [this](bool on) {
            m_metro.set_silent_after_count_in(on);
            QSettings().setValue("metronome/silentAfter", on);
            update_labels();
        });
        m_metro.set_silent_after_count_in(silent->isChecked());
    }
    lay->addWidget(m_count_in_btn);

    // Level + pan.
    m_volume = new KnobWidget("Level", -40.0, 6.0, -6.0, "dB");
    m_volume->set_scroll_step(1.0);
    m_volume->set_accent(Theme::generic_accent());
    m_volume->set_value(s.value("metronome/volume", -6.0).toDouble());
    m_metro.set_volume_db(static_cast<float>(m_volume->value()));
    connect(m_volume, &KnobWidget::value_changed, this, [this](double v) {
        m_metro.set_volume_db(static_cast<float>(v));
        QSettings().setValue("metronome/volume", v);
    });
    m_pan = new KnobWidget("Pan", -100.0, 100.0, 0.0, "%");
    m_pan->set_scroll_step(10.0);
    m_pan->set_accent(Theme::generic_accent());
    m_pan->set_value_formatter([](double v) {
        const int p = static_cast<int>(std::lround(v));
        return p == 0 ? QString("Center")
                      : QString("%1 %2").arg(std::abs(p)).arg(p < 0 ? "L" : "R");
    });
    m_pan->set_value(s.value("metronome/pan", 0.0).toDouble());
    m_metro.set_pan(static_cast<float>(m_pan->value() / 100.0));
    connect(m_pan, &KnobWidget::value_changed, this, [this](double v) {
        m_metro.set_pan(static_cast<float>(v / 100.0));
        QSettings().setValue("metronome/pan", v);
    });
    lay->addWidget(m_volume);
    lay->addWidget(m_pan);
    lay->addStretch(1);

    // Restore the rest.
    m_metro.set_subdivision(s.value("metronome/subdivision", 1).toInt());
    m_metro.set_sound(s.value("metronome/sound", 0).toInt());
    m_metro.set_count_in_bars(s.value("metronome/countIn", 0).toInt());
    const int beats = s.value("metronome/beats", 4).toInt();
    const int unit = s.value("metronome/unit", 4).toInt();
    set_meter(beats, unit, !s.contains("metronome/accents"));
    if (s.contains("metronome/accents")) {
        const uint32_t acc = s.value("metronome/accents").toUInt();
        m_metro.set_accents(acc);
        m_leds->set_pattern(m_metro.get_beats(), acc);
    }
    update_labels();
}

double TempoBar::get_bpm() const {
    return m_bpm->value();
}

void TempoBar::set_bpm(double new_bpm) {
    m_bpm->set_value(new_bpm);
}

void TempoBar::toggle_play() {
    m_play->toggle();
}

QWidget* TempoBar::action_widget(const QString& id) const {
    if (id == "action:metronome") {
        return m_play;
    }
    if (id == "action:tap") {
        return m_tap_button;
    }
    return nullptr;
}

void TempoBar::learn_menu_for(QWidget* w, const QString& action_id) {
    w->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(w, &QWidget::customContextMenuRequested, this, [this, w, action_id](const QPoint& p) {
        if (!m_learn_hook) {
            return;
        }
        QMenu menu(this);
        m_learn_hook(action_id, &menu);
        if (!menu.isEmpty()) {
            menu.exec(w->mapToGlobal(p));
        }
    });
}

void TempoBar::poll() {
    m_leds->set_beat(m_metro.is_running() ? m_metro.current_beat() : -1, m_metro.in_count_in());
}

void TempoBar::set_meter(int beats, int unit, bool reset_accents) {
    m_metro.set_meter(beats, unit);
    QSettings s;
    s.setValue("metronome/beats", m_metro.get_beats());
    s.setValue("metronome/unit", m_metro.get_unit());
    if (reset_accents) {
        const uint32_t acc = Metronome::default_accents(m_metro.get_beats(), m_metro.get_unit());
        m_metro.set_accents(acc);
        s.setValue("metronome/accents", acc);
    }
    m_leds->set_pattern(m_metro.get_beats(), m_metro.get_accents());
    update_labels();
}

void TempoBar::update_labels() {
    QSettings s;
    m_meter_btn->setText(QString("%1/%2").arg(m_metro.get_beats()).arg(m_metro.get_unit()));
    const int sub = std::clamp(s.value("metronome/subdivision", 1).toInt(), 1, 4);
    m_subdiv_btn->setText(k_subdiv_short[sub - 1]);
    m_subdiv_btn->setToolTip(QString("Subdivision: %1").arg(k_subdiv_names[sub - 1]));
    m_sound_btn->setText(k_sound_names[std::clamp(s.value("metronome/sound", 0).toInt(), 0,
                                                  Metronome::K_SOUND_COUNT - 1)]);
    const int ci = s.value("metronome/countIn", 0).toInt();
    const bool silent = s.value("metronome/silentAfter", false).toBool();
    m_count_in_btn->setText(ci == 0
                                ? QString("No count-in")
                                : QString("Count-in %1%2").arg(ci).arg(silent ? " · silent" : ""));
}

void TempoBar::tap() {
    if (!m_tap_clock.isValid()) {
        m_tap_clock.start();
    }
    const qint64 now = m_tap_clock.elapsed();
    // A pause longer than a 40 BPM beat (plus slack) starts a new measurement.
    if (!m_taps.isEmpty() && now - m_taps.last() > 2000) {
        m_taps.clear();
    }
    m_taps.append(now);
    if (m_taps.size() > 5) {
        m_taps.removeFirst(); // average the last four intervals
    }
    if (m_taps.size() < 2) {
        return;
    }
    const double avg_ms = static_cast<double>(m_taps.last() - m_taps.first()) /
                          static_cast<double>(m_taps.size() - 1);
    if (avg_ms > 0.0) {
        m_bpm->set_value(60000.0 / avg_ms);
    }
}
