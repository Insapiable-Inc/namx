#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "Common.h"
#include "ParamRegistry.h"

namespace npl
{

class AudioEngine;

// Preset schema v2 (.nampreset JSON) — the one place that maps the
// parameter registry + engine state to and from JSON. v1 files (no
// "version" key) load through the same path: absent groups leave their
// effect untouched, absent keys inside a present group fall back to the
// registered defaults.
namespace PresetSerializer
{

QJsonObject to_json(const ParamRegistry& params, AudioEngine& engine);

// Everything apply() cannot do itself — the caller owns chain rebuild and
// model/IR loading (dialogs, buttons, session keys).
struct Applied
{
    QStringList effect_order; // empty = key absent from the file
    QString nam_path; // empty = absent or saved with no model
    QString ir_path;
    bool has_ir_path_b = false; // presets before the dual cab leave slot B alone
    QString ir_path_b; // "" with hasIrPathB = slot B empty in the preset
};

// Applies all registry parameters to their widgets (the engine follows via
// the registry's signal connections) and returns the rest.
Applied apply(const QJsonObject& obj, const ParamRegistry& params);

} // namespace PresetSerializer

} // namespace npl
