#include "PresetSerializer.h"

#include <algorithm>
#include <cmath>

#include <QJsonArray>
#include <QJsonValue>

#include "AudioEngine.h"
#include "Common.h"
#include "Constants.h"
#include "KnobWidget.h"
#include "TempoSync.h"
#include "ToggleSwitch.h"

namespace namx
{

namespace PresetSerializer
{

QJsonObject to_json(const ParamRegistry& params, AudioEngine& engine) {
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

    for (const auto& p : params) {
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

QJsonObject upgrade(QJsonObject obj) {
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
    return out;
}

} // namespace PresetSerializer

} // namespace namx
