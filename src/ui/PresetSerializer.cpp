#include "PresetSerializer.h"

#include <cmath>

#include <QJsonArray>
#include <QJsonValue>

#include "AudioEngine.h"
#include "Common.h"
#include "KnobWidget.h"
#include "ToggleSwitch.h"

namespace npl
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
        if (!p.in_preset) {
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
                QString("It was saved by a newer NAM Player (preset format %1; this version "
                        "reads up to %2). Update NAM Player to load it.")
                    .arg(static_cast<int>(version))
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
        return std::unexpected(QString("It doesn't contain any NAM Player settings."));
    }
    return {};
}

Applied apply(const QJsonObject& obj, const ParamRegistry& params) {
    // A group absent from the file leaves its effect untouched (v1
    // semantics); inside a present group — and at the root — missing keys
    // fall back to the registered defaults.
    for (const auto& p : params) {
        if (!p.in_preset) {
            continue;
        }
        QJsonObject src = obj;
        if (!p.group.isEmpty()) {
            if (!obj.contains(p.group)) {
                continue;
            }
            src = obj[p.group].toObject();
        }
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

} // namespace npl
