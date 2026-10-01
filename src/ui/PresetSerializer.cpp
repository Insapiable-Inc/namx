#include "PresetSerializer.h"

#include <QJsonArray>
#include <QJsonValue>

#include "AudioEngine.h"
#include "KnobWidget.h"
#include "ToggleSwitch.h"

namespace PresetSerializer {

QJsonObject toJson(const ParamRegistry& params, AudioEngine& engine)
{
    // Widget values mirror the engine (every change flows through the
    // registry connections), so the widgets are the single source here.
    QJsonObject obj;
    obj["version"] = 2;
    obj["namPath"] = QString::fromStdString(engine.modelPath());
    obj["irPath"]  = QString::fromStdString(engine.irPath());

    QJsonArray orderArr;
    for (const auto& id : engine.chainIds())
        orderArr.append(QString::fromStdString(id));
    obj["effectOrder"] = orderArr;

    for (const auto& p : params)
    {
        if (!p.inPreset) continue;
        const QJsonValue v = p.knob ? QJsonValue(p.knob->value())
                                    : QJsonValue(p.toggle->isChecked());
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

Applied apply(const QJsonObject& obj, const ParamRegistry& params)
{
    // A group absent from the file leaves its effect untouched (v1
    // semantics); inside a present group — and at the root — missing keys
    // fall back to the registered defaults.
    for (const auto& p : params)
    {
        if (!p.inPreset) continue;
        QJsonObject src = obj;
        if (!p.group.isEmpty())
        {
            if (!obj.contains(p.group)) continue;
            src = obj[p.group].toObject();
        }
        if (p.knob)
            p.knob->setValue(src.contains(p.key) ? src[p.key].toDouble(p.knobDefault)
                                                 : p.knobDefault);
        else
            p.toggle->setChecked(src.contains(p.key) ? src[p.key].toBool(p.toggleDefault)
                                                     : p.toggleDefault);
    }

    Applied out;
    if (obj.contains("effectOrder"))
        for (const auto& v : obj["effectOrder"].toArray())
            out.effectOrder << v.toString();
    out.namPath = obj["namPath"].toString();
    out.irPath  = obj["irPath"].toString();
    return out;
}

} // namespace PresetSerializer
