#include "MidiMapper.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>

#include "Common.h"
#include "KnobWidget.h"
#include "TempoSync.h"
#include "ToggleSwitch.h"

namespace namx
{

static const char* const SOURCE_NAMES[] = {"cc", "note", "program"};
static const char* const MODE_NAMES[] = {"absolute", "relative", "toggle", "momentary"};

QJsonObject MidiMapping::to_json() const {
    QJsonObject o;
    o["enabled"] = enabled;
    o["source"] = SOURCE_NAMES[source];
    o["channel"] = channel < 0 ? 0 : channel + 1; // 0 = any, 1–16
    o["number"] = number;
    o["target"] = target;
    o["mode"] = MODE_NAMES[mode];
    o["min"] = min;
    o["max"] = max;
    return o;
}

MidiMapping MidiMapping::from_json(const QJsonObject& o) {
    MidiMapping m;
    m.enabled = o.value("enabled").toBool(true);
    const QString src = o.value("source").toString();
    for (int i = 0; i < 3; ++i) {
        if (src == SOURCE_NAMES[i]) {
            m.source = Source(i);
        }
    }
    m.channel = std::clamp(o.value("channel").toInt(0), 0, 16) - 1;
    m.number = std::clamp(o.value("number").toInt(0), 0, 127);
    m.target = o.value("target").toString();
    const QString mode = o.value("mode").toString();
    for (int i = 0; i < 4; ++i) {
        if (mode == MODE_NAMES[i]) {
            m.mode = Mode(i);
        }
    }
    m.min = std::clamp(o.value("min").toDouble(0.0), 0.0, 1.0);
    m.max = std::clamp(o.value("max").toDouble(1.0), 0.0, 1.0);
    return m;
}

bool MidiMapping::matches(const MidiMessage& m) const {
    if (!enabled || (channel >= 0 && channel != m.channel) || m.number != number) {
        return false;
    }
    switch (source) {
        case CC:
            return m.type == MidiMessage::CONTROL_CHANGE;
        case NOTE:
            return m.type == MidiMessage::NOTE_ON || m.type == MidiMessage::NOTE_OFF;
        case PROGRAM:
            return m.type == MidiMessage::PROGRAM_CHANGE;
    }
    return false;
}

MidiMapper::MidiMapper(QObject* parent)
    : QObject(parent) {
    QSettings s;
    if (s.contains("midi/mappings")) {
        const QJsonArray arr =
            QJsonDocument::fromJson(s.value("midi/mappings").toByteArray()).array();
        for (const auto& v : arr) {
            mappings << MidiMapping::from_json(v.toObject());
        }
    } else {
        mappings = default_mappings();
    }
    follow_clock = s.value("midi/followClock", false).toBool();
    clock.start();
}

void MidiMapper::add_knob(const QString& id, const QString& group, const QString& label,
                          KnobWidget* k) {
    targets.push_back({id, label, group, k, nullptr, {}});
}

void MidiMapper::add_toggle(const QString& id, const QString& group, const QString& label,
                            ToggleSwitch* t) {
    targets.push_back({id, label, group, nullptr, t, {}});
}

void MidiMapper::set_reverse_switch(const QString& id, ToggleSwitch* sw) {
    for (auto& t : targets) {
        if (t.id == id) {
            t.reverse = sw;
        }
    }
}

void MidiMapper::add_action(const QString& id, const QString& group, const QString& label,
                            std::function<void()> fn) {
    targets.push_back({id, label, group, nullptr, nullptr, std::move(fn)});
}

const MidiTarget* MidiMapper::target(const QString& id) const {
    for (const auto& t : targets) {
        if (t.id == id) {
            return &t;
        }
    }
    return nullptr;
}

const MidiTarget* MidiMapper::target_for(const QWidget* w) const {
    for (const auto& t : targets) {
        if ((t.knob && t.knob == w) || (t.toggle && t.toggle == w)) {
            return &t;
        }
    }
    return nullptr;
}

QList<MidiMapping> MidiMapper::default_mappings() {
    // The common floor-controller setup: program changes 0–7 recall slots 1–8.
    QList<MidiMapping> out;
    for (int i = 0; i < 8; ++i) {
        MidiMapping m;
        m.source = MidiMapping::PROGRAM;
        m.number = i;
        m.target = QString("action:slot%1").arg(i + 1);
        m.mode = MidiMapping::TOGGLE;
        out << m;
    }
    return out;
}

void MidiMapper::set_mappings(const QList<MidiMapping>& m) {
    mappings = m;
    last_value.clear();
    save();
    emit mappings_changed();
}

void MidiMapper::save() const {
    QJsonArray arr;
    for (const auto& m : mappings) {
        arr.append(m.to_json());
    }
    QSettings().setValue("midi/mappings", QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

bool MidiMapper::has_mapping(const QString& target_id) const {
    return std::any_of(mappings.begin(), mappings.end(), [&](const MidiMapping& m) {
        return m.target == target_id;
    });
}

QString MidiMapper::mapping_summary(const QString& target_id) const {
    QStringList parts;
    for (const auto& m : mappings) {
        if (m.target != target_id) {
            continue;
        }
        const QString ch = m.channel < 0 ? QString() : QString(" ch %1").arg(m.channel + 1);
        parts << (m.source == MidiMapping::CC     ? QString("CC %1").arg(m.number)
                  : m.source == MidiMapping::NOTE ? QString("Note %1").arg(m.number)
                                                  : QString("Program %1").arg(m.number)) +
                     ch;
    }
    return parts.join(", ");
}

void MidiMapper::forget(const QString& target_id) {
    QList<MidiMapping> kept;
    for (const auto& m : mappings) {
        if (m.target != target_id) {
            kept << m;
        }
    }
    set_mappings(kept);
}

bool MidiMapper::save_file(const QString& path) const {
    QJsonArray arr;
    for (const auto& m : mappings) {
        arr.append(m.to_json());
    }
    QJsonObject root;
    root["version"] = 1;
    root["mappings"] = arr;
    // Atomic: a failed write leaves the old file as it was.
    const QByteArray data = QJsonDocument(root).toJson();
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.commit();
}

bool MidiMapper::load_file(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = "cannot read the file";
        }
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (!doc.isObject() || !doc.object().value("mappings").isArray()) {
        if (error) {
            *error = doc.isNull() ? pe.errorString() : QString("not a MIDI mapping file");
        }
        return false;
    }
    QList<MidiMapping> list;
    for (const auto& v : doc.object().value("mappings").toArray()) {
        list << MidiMapping::from_json(v.toObject());
    }
    set_mappings(list);
    return true;
}

void MidiMapper::learn(const QString& target_id) {
    learn_target = target_id;
    pending_learn_row = -1;
    emit learn_changed();
}

void MidiMapper::learn_row(qsizetype row) {
    learn_target.clear();
    pending_learn_row = row;
    emit learn_changed();
}

void MidiMapper::cancel_learn() {
    if (learn_target.isEmpty() && pending_learn_row < 0) {
        return;
    }
    learn_target.clear();
    pending_learn_row = -1;
    emit learn_changed();
}

void MidiMapper::set_follow_clock(bool on) {
    follow_clock = on;
    QSettings().setValue("midi/followClock", on);
}

void MidiMapper::handle(const MidiMessage& m) {
    if (m.type == MidiMessage::CLOCK || m.type == MidiMessage::START ||
        m.type == MidiMessage::STOP || m.type == MidiMessage::CONTINUE) {
        handle_clock(m);
        return;
    }
    if (!m.is_voice()) {
        return;
    }
    emit activity(m.describe());

    // Learn: the next CC, note-on or program change binds. Note-offs and
    // CC zeros (a footswitch release) don't start a binding.
    const bool learnable = m.type == MidiMessage::PROGRAM_CHANGE ||
                           m.type == MidiMessage::NOTE_ON ||
                           (m.type == MidiMessage::CONTROL_CHANGE && m.value > 0);
    if (learnable && (!learn_target.isEmpty() || pending_learn_row >= 0)) {
        MidiMapping mp;
        if (pending_learn_row >= 0 && pending_learn_row < mappings.size()) {
            mp = mappings[pending_learn_row];
        }
        mp.source = m.type == MidiMessage::CONTROL_CHANGE   ? MidiMapping::CC
                    : m.type == MidiMessage::PROGRAM_CHANGE ? MidiMapping::PROGRAM
                                                            : MidiMapping::NOTE;
        mp.channel = m.channel;
        mp.number = m.number;
        QList<MidiMapping> list = mappings;
        if (pending_learn_row >= 0 && pending_learn_row < list.size()) {
            list[pending_learn_row] = mp;
        } else {
            mp.target = learn_target;
            const MidiTarget* t = target(learn_target);
            // Sensible default mode: knobs follow a CC; switches and actions
            // flip per press (footswitches), anything driven by notes too.
            mp.mode = (t && t->knob && mp.source == MidiMapping::CC) ? MidiMapping::ABSOLUTE
                                                                     : MidiMapping::TOGGLE;
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [&](const MidiMapping& x) {
                                          return x.target == learn_target;
                                      }),
                       list.end());
            list << mp;
        }
        learn_target.clear();
        pending_learn_row = -1;
        set_mappings(list);
        emit learn_changed();
        return;
    }

    for (int i = 0; i < mappings.size(); ++i) {
        if (mappings[i].matches(m)) {
            apply(mappings[i], i, m);
        }
    }
}

void MidiMapper::apply(MidiMapping& mp, int index, const MidiMessage& m) {
    const MidiTarget* t = target(mp.target);
    if (!t) {
        return;
    }

    // Normalize the message to a 0…127 value + "pressed" edge.
    int value = 0;
    switch (m.type) {
        case MidiMessage::CONTROL_CHANGE:
            value = m.value;
            break;
        case MidiMessage::NOTE_ON:
            value = 127;
            break;
        case MidiMessage::NOTE_OFF:
            value = 0;
            break;
        case MidiMessage::PROGRAM_CHANGE:
            value = 127;
            break;
        default:
            return;
    }
    const int last = last_value.value(index, 0);
    const bool pressed = value >= 64 && (last < 64 || m.type == MidiMessage::PROGRAM_CHANGE);
    last_value[index] = m.type == MidiMessage::PROGRAM_CHANGE ? 0 : value;

    if (t->action) {
        if (pressed) {
            t->action();
        }
        return;
    }

    if (t->toggle) {
        switch (mp.mode) {
            case MidiMapping::TOGGLE:
            case MidiMapping::RELATIVE:
                if (pressed) {
                    t->toggle->setChecked(!t->toggle->isChecked());
                }
                break;
            case MidiMapping::ABSOLUTE:
            case MidiMapping::MOMENTARY:
                if (m.type != MidiMessage::PROGRAM_CHANGE) {
                    t->toggle->setChecked(value >= 64);
                } else {
                    t->toggle->setChecked(!t->toggle->isChecked());
                }
                break;
        }
        return;
    }

    if (t->knob) {
        KnobWidget* k = t->knob;
        double lo = k->minimum() + mp.min * (k->maximum() - k->minimum());
        double hi = k->minimum() + mp.max * (k->maximum() - k->minimum());
        if (t->reverse && t->reverse->isChecked()) {
            std::swap(lo, hi); // heel ↔ toe
        }
        switch (mp.mode) {
            case MidiMapping::ABSOLUTE:
                k->set_value(lo + (hi - lo) * value / 127.0);
                break;
            case MidiMapping::RELATIVE: {
                if (m.type != MidiMessage::CONTROL_CHANGE) {
                    break;
                }
                const int delta = m.value < 64 ? m.value : m.value - 128; // two's complement
                const double step = (hi - lo) / 127.0;
                k->set_value(
                    std::clamp(k->value() + delta * step, std::min(lo, hi), std::max(lo, hi)));
                break;
            }
            case MidiMapping::TOGGLE:
                if (pressed) {
                    // The knob quantizes what it is set to (dial steps, log taper,
                    // snap), so "is it at hi" is "is it nearer hi than lo" (audit
                    // 2026-10-04, Fable item 05).
                    const double v = k->value();
                    const bool at_hi = std::abs(v - hi) < std::abs(v - lo);
                    k->set_value(at_hi ? lo : hi);
                }
                break;
            case MidiMapping::MOMENTARY:
                k->set_value(value >= 64 ? hi : lo);
                break;
        }
    }
}

void MidiMapper::handle_clock(const MidiMessage& m) {
    if (m.type != MidiMessage::CLOCK) {
        beat_start_us = -1; // transport change: restart the measurement
        clock_ticks = 0;
        beat_ms.clear();
        return;
    }
    if (!follow_clock) {
        return;
    }
    // Reception time, not handling time: ticks queued behind a busy GUI arrive here
    // in a burst (audit 2026-10-04 item 05).
    const qint64 now = m.received_us > 0 ? m.received_us : clock.nsecsElapsed() / 1000;
    if (beat_start_us < 0 || now - beat_start_us > 2'000'000) // first tick / clock resumed
    {
        beat_start_us = now;
        clock_ticks = 0;
        beat_ms.clear();
        return;
    }
    if (++clock_ticks < 24) {
        return; // 24 ticks per quarter note
    }
    beat_ms << static_cast<double>(now - beat_start_us) / 1000.0;
    if (beat_ms.size() > 4) {
        beat_ms.removeFirst();
    }
    beat_start_us = now;
    clock_ticks = 0;

    double sum = 0.0;
    for (double b : beat_ms) {
        sum += b;
    }
    const double avg_ms = sum / static_cast<double>(beat_ms.size());
    const double bpm = avg_ms > 0.0 ? std::round(60000.0 / avg_ms * 10.0) / 10.0 : 0.0;
    // Unstamped ticks handled in one burst would read as thousands of BPM: a tempo
    // the tempo bar can't show is a measurement error, not a tempo.
    if (!std::isfinite(bpm) || bpm < TempoSync::MIN_BPM || bpm > TempoSync::MAX_BPM) {
        return;
    }
    if (std::abs(bpm - last_clock_bpm) >= 0.2) // ignore jitter
    {
        last_clock_bpm = bpm;
        emit tempo_from_clock(bpm);
    }
}

} // namespace namx
