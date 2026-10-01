#include "PresetSerializer.h"

#include <QJsonArray>
#include <QJsonValue>

#include "AudioEngine.h"
#include "KnobWidget.h"
#include "ToggleSwitch.h"

namespace PresetSerializer
{

QJsonObject to_json(const ParamRegistry& params, AudioEngine& engine) {
    // Widget values mirror the engine (every change flows through the
    // registry connections), so the widgets are the single source here.
    QJsonObject obj;
    obj["version"] = 2;
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
