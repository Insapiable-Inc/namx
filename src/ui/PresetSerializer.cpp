#include "PresetSerializer.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QJsonArray>
#include <QJsonValue>

#include "AudioEngine.h"
#include "Bf3Flanger.h"
#include "Ce5Chorus.h"
#include "Common.h"
#include "Constants.h"
#include "Cs3Compressor.h"
#include "Dd8Delay.h"
#include "Ge7Eq.h"
#include "KnobWidget.h"
#include "Ph3Phaser.h"
#include "TempoSync.h"
#include "ToggleSwitch.h"
#include "Tr2Tremolo.h"

namespace namx
{

namespace PresetSerializer
{

QJsonObject to_json(const ParamRegistry& params, AudioEngine& engine, std::optional<double> tempo) {
    // Widget values mirror the engine (every change flows through the
    // registry connections), so the widgets are the single source here.
    QJsonObject obj;
    obj["version"] = VERSION;
    obj["namPath"] = QString::fromStdString(engine.model_path());
    obj["irPath"] = QString::fromStdString(engine.ir_path());
    obj["irPathB"] = QString::fromStdString(engine.ir_path_b()); // "" = slot B empty

    QJsonArray order_arr;
    for (const auto& id : engine.chain_ids()) {
        order_arr.append(QString::fromStdString(id));
    }
    obj["effectOrder"] = order_arr;
    if (tempo) {
        obj["tempo"] = *tempo;
    }

    for (const auto& p : params) {
        if (p.scope == ParamScope::GLOBAL) {
            continue;
        }
        const QJsonValue v =
            p.knob ? QJsonValue(p.knob->value()) : QJsonValue(p.toggle->isChecked());
        if (p.group.isEmpty()) {
            obj[p.key] = v;
        } else {
            QJsonObject g = obj[p.group].toObject();
            g[p.key] = v;
            obj[p.group] = g;
        }
    }
    return obj;
}

expected<void, QString> validate(const QJsonObject& obj, const ParamRegistry& params) {
    if (obj.contains("version")) {
        const QJsonValue v = obj["version"];
        const double version = v.toDouble(-1.0);
        if (!v.isDouble() || version != std::floor(version) || version < 1.0) {
            return std::unexpected(QString("Its \"version\" field is invalid."));
        }
        if (version > VERSION) {
            return std::unexpected(
                QString("It was saved by a newer NAM-X (preset format %1; this version "
                        "reads up to %2). Update NAM-X to load it.")
                    .arg(QString::number(version, 'g', 15))
                    .arg(VERSION));
        }
    }

    auto type_error = [](const QString& key, const char* expected_type) {
        return std::unexpected(QString("\"%1\" should be %2.").arg(key, expected_type));
    };
    bool known = false; // at least one key this build understands
    for (const auto& p : params) {
        if (p.scope == ParamScope::GLOBAL) {
            continue; // ignored on load, whatever it holds
        }
        const QJsonObject* src = &obj;
        QJsonObject group;
        const QString name = p.group.isEmpty() ? p.key : p.group + "/" + p.key;
        if (!p.group.isEmpty()) {
            if (!obj.contains(p.group)) {
                continue;
            }
            if (!obj[p.group].isObject()) {
                return type_error(p.group, "a group of settings");
            }
            group = obj[p.group].toObject();
            src = &group;
        }
        if (!src->contains(p.key)) {
            continue;
        }
        known = true;
        const QJsonValue v = (*src)[p.key];
        if (p.knob && !(v.isDouble() && std::isfinite(v.toDouble()))) {
            return type_error(name, "a number");
        }
        if (p.toggle && !v.isBool()) {
            return type_error(name, "true or false");
        }
    }
    for (const char* key : {"namPath", "irPath", "irPathB"}) {
        if (obj.contains(key)) {
            if (!obj[key].isString()) {
                return type_error(key, "a file path");
            }
            known = true;
        }
    }
    if (obj.contains("tempo")) {
        if (!(obj["tempo"].isDouble() && std::isfinite(obj["tempo"].toDouble()))) {
            return type_error("tempo", "a number");
        }
        known = true;
    }
    if (obj.contains("effectOrder")) {
        if (!obj["effectOrder"].isArray()) {
            return type_error("effectOrder", "a list of effect names");
        }
        for (const auto& id : obj["effectOrder"].toArray()) {
            if (!id.isString()) {
                return type_error("effectOrder", "a list of effect names");
            }
        }
        known = true;
    }
    if (!known) {
        return std::unexpected(QString("It doesn't contain any NAM-X settings."));
    }
    return {};
}

// NAM-X's own Plate, Hall and Room reverbs (reverb/type 0–2) were removed on 2026-11-03: they
// load as the RV-6's Plate, Hall and Room (5, 6, 7).
static void upgrade_reverb_type(QJsonObject& obj) {
    QJsonObject reverb = obj["reverb"].toObject();
    if (!reverb.contains("type")) {
        return;
    }
    const long old_type = std::lround(reverb["type"].toDouble());
    if (old_type >= 0 && old_type <= 2) {
        reverb["type"] = static_cast<double>(old_type + 5);
        obj["reverb"] = reverb;
    }
}

// The Equalizer's sliders were on the GE-7's printed scale until 2026-11-09 (the marks
// aren't the dB they say): each becomes the dB its mark gave.
static QJsonObject upgrade_eq(QJsonObject obj) {
    QJsonObject eq = obj["eq"].toObject();
    bool changed = false;
    for (size_t i = 0; i < EQ_PRINTED_KEYS.size(); ++i) {
        if (!eq.contains(EQ_PRINTED_KEYS[i])) {
            continue;
        }
        const QString key = eq_slider_key(i);
        if (!eq.contains(key)) {
            eq[key] = ge7::printed_mark_db(i, eq[EQ_PRINTED_KEYS[i]].toDouble(0.0));
        }
        eq.remove(EQ_PRINTED_KEYS[i]);
        changed = true;
    }
    if (changed) {
        obj["eq"] = eq;
    }
    return obj;
}

QString eq_slider_key(size_t slider) {
    return slider < ge7::BAND_COUNT ? QString("gain%1").arg(slider + 1) : QString("level");
}

// Until preset version 3 the Tremolo's Depth and Wave were positions on the TR-2's knobs
// (0–100 % = 7:00–5:00): each becomes the value that sounds the same on today's plain curves
// (Wave's depends on the rate: the saved one, also when the preset syncs it to the tempo).
static QJsonObject upgrade_tremolo(QJsonObject obj) {
    if (obj["version"].toDouble(1.0) >= 3.0 || !obj.contains("tremolo")) {
        return obj;
    }
    QJsonObject tremolo = obj["tremolo"].toObject();
    if (tremolo.contains("depth")) {
        const float position = static_cast<float>(tremolo["depth"].toDouble() / 100.0);
        tremolo["depth"] = 100.0 * tr2::depth_from_pedal(tr2::SUPPLY, position);
    }
    if (tremolo.contains("wave")) {
        const float position = static_cast<float>(tremolo["wave"].toDouble() / 100.0);
        const float rate =
            static_cast<float>(tremolo["rate"].toDouble(NAMConstants::TREMOLO_RATE_DEFAULT));
        tremolo["wave"] = 100.0 * tr2::wave_from_pedal(tr2::SUPPLY, position, rate);
    }
    obj["tremolo"] = tremolo;
    return obj;
}

// Until preset version 4 the Delay's Feedback (0–90 %) and Mix (0–100 %) were positions on
// the DD-8's F.Back and E.Level knobs (7:00–5:00): each becomes the value that sounds the
// same on today's plain curves, in the preset's own mode (Feedback as the gain per round,
// which stops at 100 %: the pedal's top, a little over, is a hold now; Mix as the repeats'
// level in dB).
static QJsonObject upgrade_delay_knobs(QJsonObject obj) {
    if (obj["version"].toDouble(1.0) >= 4.0 || !obj.contains("delay")) {
        return obj;
    }
    using namespace NAMConstants;
    QJsonObject delay = obj["delay"].toObject();
    const int type = std::clamp(static_cast<int>(delay["type"].toDouble(1.0)), 1,
                                static_cast<int>(dd8::VOICINGS.size()));
    const dd8::Voicing& voicing = *dd8::VOICINGS[static_cast<size_t>(type - 1)];
    if (delay.contains("feedback")) {
        const float position = static_cast<float>(delay["feedback"].toDouble() / 90.0);
        delay["feedback"] = static_cast<double>(dd8::feedback_pct_from_pedal(voicing, position));
    }
    if (delay.contains("mix")) {
        const float position = static_cast<float>(delay["mix"].toDouble() / 100.0);
        delay["mix"] = std::clamp(static_cast<double>(dd8::level_db_from_pedal(voicing, position)),
                                  DELAY_LEVEL_MIN, DELAY_LEVEL_MAX);
    }
    obj["delay"] = delay;
    return obj;
}

// Until preset version 5 the Chorus's Depth and Mix (0–100 %) were positions on the CE-5's
// Depth and E.Level knobs (7:00–5:00), until version 6 its Low and High too: each becomes the
// value that sounds the same on today's plain curves (Depth along the sweep's width, Mix as the
// chorus's level in dB, Low and High as cut frequencies).
static QJsonObject upgrade_chorus_knobs(QJsonObject obj) {
    const double version = obj["version"].toDouble(1.0);
    if (version >= 6.0 || !obj.contains("chorus")) {
        return obj;
    }
    using namespace NAMConstants;
    QJsonObject chorus = obj["chorus"].toObject();
    if (chorus.contains("ce5Low")) {
        const float position = static_cast<float>(chorus["ce5Low"].toDouble() / 100.0);
        chorus["ce5Low"] = static_cast<double>(ce5::low_hz_from_pedal(position));
    }
    if (chorus.contains("ce5High")) {
        const float position = static_cast<float>(chorus["ce5High"].toDouble() / 100.0);
        chorus["ce5High"] = static_cast<double>(ce5::high_hz_from_pedal(position));
    }
    if (version >= 5.0) {
        obj["chorus"] = chorus;
        return obj;
    }
    if (chorus.contains("depth")) {
        const float position = static_cast<float>(chorus["depth"].toDouble() / 100.0);
        chorus["depth"] = 100.0 * static_cast<double>(ce5::depth_from_pedal(position));
    }
    if (chorus.contains("mix")) {
        const float position = static_cast<float>(chorus["mix"].toDouble() / 100.0);
        chorus["mix"] = std::clamp(static_cast<double>(ce5::level_db_from_pedal(position)),
                                   CHORUS_LEVEL_MIN, CHORUS_LEVEL_MAX);
    }
    obj["chorus"] = chorus;
    return obj;
}

// Until preset version 7 the Flanger's Range (0–100 %, 100 = the long end), Depth (0–100 %) and
// Feedback (0–99 %) were positions on the BF-3's Manual, Depth and Res knobs: each becomes the
// value that sounds the same on today's plain curves (Manual's delay in ms, Depth along the
// sweep's width, Feedback as the gain per pass in %). Type 3, the refit beside the first
// Standard, is Standard since 2026-11-10 (any version).
static QJsonObject upgrade_flanger(QJsonObject obj) {
    if (!obj.contains("flanger")) {
        return obj;
    }
    QJsonObject flanger = obj["flanger"].toObject();
    if (static_cast<int>(flanger["type"].toDouble(1.0)) == 3) {
        flanger["type"] = 1.0;
    }
    if (obj["version"].toDouble(1.0) < 7.0) {
        if (flanger.contains("range")) {
            const float position = 1.0f - static_cast<float>(flanger["range"].toDouble() / 100.0);
            flanger["range"] = static_cast<double>(bf3::manual_ms_from_pedal(position));
        }
        if (flanger.contains("depth")) {
            const float position = static_cast<float>(flanger["depth"].toDouble() / 100.0);
            flanger["depth"] = 100.0 * static_cast<double>(bf3::depth_from_pedal(position));
        }
        if (flanger.contains("feedback")) {
            const float position = static_cast<float>(flanger["feedback"].toDouble() / 99.0);
            flanger["feedback"] = 100.0 * static_cast<double>(bf3::feedback_from_pedal(position));
        }
    }
    obj["flanger"] = flanger;
    return obj;
}

// Until preset version 8 the Phaser's Depth (0–100 %) and Feedback (0–90 %) were positions on
// the PH-3's Depth and Res knobs: each becomes the value that sounds the same on today's plain
// curves (Depth as the share of the sweep's travel, Feedback as the loop's gain per pass in %,
// in the preset's own mode).
static QJsonObject upgrade_phaser(QJsonObject obj) {
    if (obj["version"].toDouble(1.0) >= 8.0 || !obj.contains("phaser")) {
        return obj;
    }
    QJsonObject phaser = obj["phaser"].toObject();
    const int type = std::clamp(static_cast<int>(phaser["type"].toDouble(1.0)), 1, ph3::MODE_COUNT);
    if (phaser.contains("depth")) {
        const float position = static_cast<float>(phaser["depth"].toDouble() / 100.0);
        phaser["depth"] = 100.0 * static_cast<double>(ph3::depth_from_pedal(position));
    }
    if (phaser.contains("feedback")) {
        const float position = static_cast<float>(phaser["feedback"].toDouble() / 90.0);
        phaser["feedback"] =
            100.0 * static_cast<double>(ph3::feedback_from_pedal(ph3::voicing(type - 1), position));
    }
    obj["phaser"] = phaser;
    return obj;
}

// Until preset version 9 the Compressor's Release was the gain's 63 % recovery after a 30 dB
// drop (44–458 ms over the CS-3's Attack knob): it becomes the time scale of the recovery at
// the same position of that knob (99.5–1152 ms).
static QJsonObject upgrade_compressor(QJsonObject obj) {
    if (obj["version"].toDouble(1.0) >= 9.0 || !obj.contains("compressor")) {
        return obj;
    }
    QJsonObject compressor = obj["compressor"].toObject();
    if (compressor.contains("release")) {
        const float recovery = static_cast<float>(compressor["release"].toDouble());
        compressor["release"] = static_cast<double>(cs3::release_from_recovery(recovery));
    }
    obj["compressor"] = compressor;
    return obj;
}

// Until preset version 10 a Flanger or Phaser `mix` was the removed classic effects' (50 % =
// the full effect; the Boss models had none): dropped, so today's Wet, on that key, loads at
// its default, the pedal alone.
static QJsonObject upgrade_modulation_mix(QJsonObject obj) {
    if (obj["version"].toDouble(1.0) >= 10.0) {
        return obj;
    }
    for (const char* group : {"flanger", "phaser"}) {
        if (obj[group].toObject().contains("mix")) {
            QJsonObject effect = obj[group].toObject();
            effect.remove("mix");
            obj[group] = effect;
        }
    }
    return obj;
}

QJsonObject upgrade(QJsonObject obj) {
    upgrade_reverb_type(obj);
    obj = upgrade_eq(std::move(obj));
    obj = upgrade_tremolo(std::move(obj));
    obj = upgrade_delay_knobs(std::move(obj));
    obj = upgrade_chorus_knobs(std::move(obj));
    obj = upgrade_flanger(std::move(obj));
    obj = upgrade_phaser(std::move(obj));
    obj = upgrade_compressor(std::move(obj));
    obj = upgrade_modulation_mix(std::move(obj));
    QJsonObject delay = obj["delay"].toObject();
    if (!delay.contains("rightRatio")) {
        return obj;
    }
    using namespace NAMConstants;
    const double share = std::clamp(delay["rightRatio"].toDouble(100.0), 25.0, 200.0) / 100.0;
    delay.remove("rightRatio");
    if (!delay.contains("rightTime")) {
        delay["rightTime"] = std::clamp(delay["time"].toDouble(DELAY_TIME_DEFAULT) * share,
                                        DELAY_TIME_MIN, DELAY_TIME_MAX);
    }
    if (!delay.contains("rightSyncNote")) {
        const double wholes =
            TempoSync::NOTES[TempoSync::clamp_index(delay["syncNote"].toDouble(8.0 /* 1/8. */))]
                .wholes *
            share;
        size_t nearest = 0;
        for (size_t i = 1; i < TempoSync::NOTES.size(); ++i) {
            if (std::abs(std::log(TempoSync::NOTES[i].wholes / wholes)) <
                std::abs(std::log(TempoSync::NOTES[nearest].wholes / wholes))) {
                nearest = i;
            }
        }
        delay["rightSyncNote"] = static_cast<double>(nearest);
    }
    obj["delay"] = delay;
    return obj;
}

Applied apply(const QJsonObject& preset, const ParamRegistry& params) {
    const QJsonObject obj = upgrade(preset);
    // A preset is the whole rig: every missing key — a whole group too, e.g. an effect
    // added after the file was saved — falls back to the registered default (effects
    // off). Leaving such effects untouched made them keep the previous preset's state.
    for (const auto& p : params) {
        if (p.scope == ParamScope::GLOBAL) {
            continue;
        }
        const QJsonObject src = p.group.isEmpty() ? obj : obj[p.group].toObject();
        if (p.knob) {
            p.knob->set_value(src.contains(p.key) ? src[p.key].toDouble(p.knob_default)
                                                  : p.knob_default);
        } else {
            p.toggle->setChecked(src.contains(p.key) ? src[p.key].toBool(p.toggle_default)
                                                     : p.toggle_default);
        }
    }

    Applied out;
    if (obj.contains("effectOrder")) {
        for (const auto& v : obj["effectOrder"].toArray()) {
            out.effect_order << v.toString();
        }
    }
    out.nam_path = obj["namPath"].toString();
    out.ir_path = obj["irPath"].toString();
    out.has_ir_path_b = obj.contains("irPathB");
    out.ir_path_b = obj["irPathB"].toString();
    if (obj.contains("tempo")) {
        out.tempo = obj["tempo"].toDouble();
    }
    return out;
}

} // namespace PresetSerializer

} // namespace namx
