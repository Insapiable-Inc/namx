#include "MidiMapper.h"

#include <algorithm>
#include <cmath>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>

#include "KnobWidget.h"
#include "ToggleSwitch.h"

// ---------------------------------------------------------------------------
// MidiMapping
// ---------------------------------------------------------------------------

static const char* kSourceNames[] = {"cc", "note", "program"};
static const char* kModeNames[] = {"absolute", "relative", "toggle", "momentary"};

QJsonObject MidiMapping::toJson() const {
    QJsonObject o;
    o["enabled"] = enabled;
    o["source"] = kSourceNames[source];
    o["channel"] = channel < 0 ? 0 : channel + 1; // 0 = any, 1–16
    o["number"] = number;
    o["target"] = target;
    o["mode"] = kModeNames[mode];
    o["min"] = min;
    o["max"] = max;
    return o;
}

MidiMapping MidiMapping::fromJson(const QJsonObject& o) {
    MidiMapping m;
    m.enabled = o.value("enabled").toBool(true);
    const QString src = o.value("source").toString();
    for (int i = 0; i < 3; ++i) {
        if (src == kSourceNames[i]) {
            m.source = Source(i);
        }
    }
    m.channel = std::clamp(o.value("channel").toInt(0), 0, 16) - 1;
    m.number = std::clamp(o.value("number").toInt(0), 0, 127);
    m.target = o.value("target").toString();
    const QString mode = o.value("mode").toString();
    for (int i = 0; i < 4; ++i) {
        if (mode == kModeNames[i]) {
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
            return m.type == MidiMessage::ControlChange;
        case Note:
            return m.type == MidiMessage::NoteOn || m.type == MidiMessage::NoteOff;
        case Program:
            return m.type == MidiMessage::ProgramChange;
    }
    return false;
}

// ---------------------------------------------------------------------------
// MidiMapper
// ---------------------------------------------------------------------------

MidiMapper::MidiMapper(QObject* parent)
    : QObject(parent) {
    QSettings s;
    if (s.contains("midi/mappings")) {
        const QJsonArray arr =
            QJsonDocument::fromJson(s.value("midi/mappings").toByteArray()).array();
        for (const auto& v : arr) {
            mMappings << MidiMapping::fromJson(v.toObject());
        }
    } else {
        mMappings = defaultMappings();
    }
    mFollowClock = s.value("midi/followClock", false).toBool();
    mClock.start();
}

void MidiMapper::addKnob(const QString& id, const QString& group, const QString& label,
                         KnobWidget* k) {
    mTargets.push_back({id, label, group, k, nullptr, {}});
}

void MidiMapper::addToggle(const QString& id, const QString& group, const QString& label,
                           ToggleSwitch* t) {
    mTargets.push_back({id, label, group, nullptr, t, {}});
}

void MidiMapper::addAction(const QString& id, const QString& group, const QString& label,
                           std::function<void()> fn) {
    mTargets.push_back({id, label, group, nullptr, nullptr, std::move(fn)});
}

const MidiTarget* MidiMapper::target(const QString& id) const {
    for (const auto& t : mTargets) {
        if (t.id == id) {
            return &t;
        }
    }
    return nullptr;
}

const MidiTarget* MidiMapper::targetFor(const QWidget* w) const {
    for (const auto& t : mTargets) {
        if ((t.knob && t.knob == w) || (t.toggle && t.toggle == w)) {
            return &t;
        }
    }
    return nullptr;
}

QList<MidiMapping> MidiMapper::defaultMappings() {
    // The common floor-controller setup: program changes 0–7 recall slots 1–8.
    QList<MidiMapping> out;
    for (int i = 0; i < 8; ++i) {
        MidiMapping m;
        m.source = MidiMapping::Program;
        m.number = i;
        m.target = QString("action:slot%1").arg(i + 1);
        m.mode = MidiMapping::Toggle;
        out << m;
    }
    return out;
}

void MidiMapper::setMappings(const QList<MidiMapping>& m) {
    mMappings = m;
    mLastValue.clear();
    save();
    emit mappingsChanged();
}

void MidiMapper::save() const {
    QJsonArray arr;
    for (const auto& m : mMappings) {
        arr.append(m.toJson());
    }
    QSettings().setValue("midi/mappings", QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

bool MidiMapper::hasMapping(const QString& targetId) const {
    return std::any_of(mMappings.begin(), mMappings.end(), [&](const MidiMapping& m) {
        return m.target == targetId;
    });
}

QString MidiMapper::mappingSummary(const QString& targetId) const {
    QStringList parts;
    for (const auto& m : mMappings) {
        if (m.target != targetId) {
            continue;
        }
        const QString ch = m.channel < 0 ? QString() : QString(" ch %1").arg(m.channel + 1);
        parts << (m.source == MidiMapping::CC     ? QString("CC %1").arg(m.number)
                  : m.source == MidiMapping::Note ? QString("Note %1").arg(m.number)
                                                  : QString("Program %1").arg(m.number)) +
                     ch;
    }
    return parts.join(", ");
}

void MidiMapper::forget(const QString& targetId) {
    QList<MidiMapping> kept;
    for (const auto& m : mMappings) {
        if (m.target != targetId) {
            kept << m;
        }
    }
    setMappings(kept);
}

bool MidiMapper::saveFile(const QString& path) const {
    QJsonArray arr;
    for (const auto& m : mMappings) {
        arr.append(m.toJson());
    }
    QJsonObject root;
    root["version"] = 1;
    root["mappings"] = arr;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    f.write(QJsonDocument(root).toJson());
    return true;
}

bool MidiMapper::loadFile(const QString& path, QString* error) {
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
        list << MidiMapping::fromJson(v.toObject());
    }
    setMappings(list);
    return true;
}

void MidiMapper::learn(const QString& targetId) {
    mLearnTarget = targetId;
    mLearnRow = -1;
    emit learnChanged();
}

void MidiMapper::learnRow(qsizetype row) {
    mLearnTarget.clear();
    mLearnRow = row;
    emit learnChanged();
}

void MidiMapper::cancelLearn() {
    if (mLearnTarget.isEmpty() && mLearnRow < 0) {
        return;
    }
    mLearnTarget.clear();
    mLearnRow = -1;
    emit learnChanged();
}

void MidiMapper::setFollowClock(bool on) {
    mFollowClock = on;
    QSettings().setValue("midi/followClock", on);
}

void MidiMapper::handle(const MidiMessage& m) {
    if (m.type == MidiMessage::Clock || m.type == MidiMessage::Start ||
        m.type == MidiMessage::Stop || m.type == MidiMessage::Continue) {
        handleClock(m);
        return;
    }
    if (!m.isVoice()) {
        return;
    }
    emit activity(m.describe());

    // Learn: the next CC, note-on or program change binds. Note-offs and
    // CC zeros (a footswitch release) don't start a binding.
    const bool learnable = m.type == MidiMessage::ProgramChange || m.type == MidiMessage::NoteOn ||
                           (m.type == MidiMessage::ControlChange && m.value > 0);
    if (learnable && (!mLearnTarget.isEmpty() || mLearnRow >= 0)) {
        MidiMapping mp;
        if (mLearnRow >= 0 && mLearnRow < mMappings.size()) {
            mp = mMappings[mLearnRow];
        }
        mp.source = m.type == MidiMessage::ControlChange   ? MidiMapping::CC
                    : m.type == MidiMessage::ProgramChange ? MidiMapping::Program
                                                           : MidiMapping::Note;
        mp.channel = m.channel;
        mp.number = m.number;
        QList<MidiMapping> list = mMappings;
        if (mLearnRow >= 0 && mLearnRow < list.size()) {
            list[mLearnRow] = mp;
        } else {
            mp.target = mLearnTarget;
            const MidiTarget* t = target(mLearnTarget);
            // Sensible default mode: knobs follow a CC; switches and actions
            // flip per press (footswitches), anything driven by notes too.
            mp.mode = (t && t->knob && mp.source == MidiMapping::CC) ? MidiMapping::Absolute
                                                                     : MidiMapping::Toggle;
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [&](const MidiMapping& x) {
                                          return x.target == mLearnTarget;
                                      }),
                       list.end());
            list << mp;
        }
        mLearnTarget.clear();
        mLearnRow = -1;
        setMappings(list);
        emit learnChanged();
        return;
    }

    for (int i = 0; i < mMappings.size(); ++i) {
        if (mMappings[i].matches(m)) {
            apply(mMappings[i], i, m);
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
        case MidiMessage::ControlChange:
            value = m.value;
            break;
        case MidiMessage::NoteOn:
            value = 127;
            break;
        case MidiMessage::NoteOff:
            value = 0;
            break;
        case MidiMessage::ProgramChange:
            value = 127;
            break;
        default:
            return;
    }
    const int last = mLastValue.value(index, 0);
    const bool pressed = value >= 64 && (last < 64 || m.type == MidiMessage::ProgramChange);
    mLastValue[index] = m.type == MidiMessage::ProgramChange ? 0 : value;

    if (t->action) {
        if (pressed) {
            t->action();
        }
        return;
    }

    if (t->toggle) {
        switch (mp.mode) {
            case MidiMapping::Toggle:
            case MidiMapping::Relative:
                if (pressed) {
                    t->toggle->setChecked(!t->toggle->isChecked());
                }
                break;
            case MidiMapping::Absolute:
            case MidiMapping::Momentary:
                if (m.type != MidiMessage::ProgramChange) {
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
        const double lo = k->minimum() + mp.min * (k->maximum() - k->minimum());
        const double hi = k->minimum() + mp.max * (k->maximum() - k->minimum());
        switch (mp.mode) {
            case MidiMapping::Absolute:
                k->setValue(lo + (hi - lo) * value / 127.0);
                break;
            case MidiMapping::Relative: {
                if (m.type != MidiMessage::ControlChange) {
                    break;
                }
                const int delta = m.value < 64 ? m.value : m.value - 128; // two's complement
                const double step = (hi - lo) / 127.0;
                k->setValue(
                    std::clamp(k->value() + delta * step, std::min(lo, hi), std::max(lo, hi)));
                break;
            }
            case MidiMapping::Toggle:
                if (pressed) {
                    k->setValue(std::abs(k->value() - hi) < 1e-9 ? lo : hi);
                }
                break;
            case MidiMapping::Momentary:
                k->setValue(value >= 64 ? hi : lo);
                break;
        }
    }
}

void MidiMapper::handleClock(const MidiMessage& m) {
    if (m.type != MidiMessage::Clock) {
        mBeatStartMs = -1; // transport change: restart the measurement
        mClockTicks = 0;
        mBeatMs.clear();
        return;
    }
    if (!mFollowClock) {
        return;
    }
    const qint64 now = mClock.elapsed();
    if (mBeatStartMs < 0 || now - mBeatStartMs > 2000) // first tick / clock resumed
    {
        mBeatStartMs = now;
        mClockTicks = 0;
        mBeatMs.clear();
        return;
    }
    if (++mClockTicks < 24) {
        return; // 24 ticks per quarter note
    }
    mBeatMs << static_cast<double>(now - mBeatStartMs);
    if (mBeatMs.size() > 4) {
        mBeatMs.removeFirst();
    }
    mBeatStartMs = now;
    mClockTicks = 0;

    double sum = 0.0;
    for (double b : mBeatMs) {
        sum += b;
    }
    const double avgMs = sum / static_cast<double>(mBeatMs.size());
    const double bpm = std::round(60000.0 / avgMs * 10.0) / 10.0;
    if (std::abs(bpm - mLastClockBpm) >= 0.2) // ignore jitter
    {
        mLastClockBpm = bpm;
        emit tempoFromClock(bpm);
    }
}
