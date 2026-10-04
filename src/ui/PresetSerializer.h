#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "Common.h"
#include "ParamRegistry.h"

namespace namx
{

class AudioEngine;

// Preset schema v2 (.nampreset JSON) — the one place that maps the
// parameter registry + engine state to and from JSON. v1 files (no
// "version" key) load through the same path. Any absent key — or whole group,
// e.g. an effect newer than the file — falls back to the registered default.
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

// Newest schema this build reads and writes.
inline constexpr int VERSION = 2;

// Checks that `obj` is a preset this build can apply, before anything changes:
// a supported `version` (absent = v1), at least one known setting, and the right
// JSON type for every known key (groups are objects, knobs numbers, switches
// booleans, paths strings, effectOrder a list of strings). Unknown keys are allowed.
// The error says what's wrong, for the user.
expected<void, QString> validate(const QJsonObject& obj, const ParamRegistry& params);

// Applies all registry parameters to their widgets (the engine follows via
// the registry's signal connections) and returns the rest. Call validate() first.
Applied apply(const QJsonObject& obj, const ParamRegistry& params);

} // namespace PresetSerializer

} // namespace namx
