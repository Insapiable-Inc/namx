#include "ThemeFile.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace namx
{

// Theme files are a few hundred bytes; anything this big is the wrong file.
static constexpr qint64 MAX_THEME_BYTES = 1 << 20;
static const char* const BREEZE_BLUE = "#3daee9";

QColor mix_colors(const QColor& a, const QColor& b, double t) {
    return QColor::fromRgbF(static_cast<float>(a.redF() + (b.redF() - a.redF()) * t),
                            static_cast<float>(a.greenF() + (b.greenF() - a.greenF()) * t),
                            static_cast<float>(a.blueF() + (b.blueF() - a.blueF()) * t));
}

// The colours a file may leave out. Shadow and disabled text follow KDE's own recipe
// closely enough to land within a few steps of what Qt reports for Breeze Dark / Light
// (shadow: the window darkened; disabled: the text faded 65 % toward the window).
static void fill_defaults(ThemeSpec& spec) {
    const bool dark = spec.window.lightness() < 128;
    if (!spec.base.isValid()) {
        spec.base = spec.window;
    }
    if (!spec.button.isValid()) {
        spec.button = spec.window;
    }
    if (!spec.header.isValid()) {
        spec.header = spec.window;
    }
    if (!spec.accent.isValid()) {
        spec.accent = QColor(BREEZE_BLUE);
    }
    if (!spec.shadow.isValid()) {
        spec.shadow = mix_colors(spec.window, Qt::black, dark ? 0.55 : 0.7);
    }
    if (!spec.disabled_text.isValid()) {
        spec.disabled_text = mix_colors(spec.text, spec.window, 0.65);
    }
}

static expected<QByteArray, QString> read_small_file(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(QString("Can't open the file: %1").arg(file.errorString()));
    }
    if (file.size() > MAX_THEME_BYTES) {
        return std::unexpected(QString("The file is too big to be a theme."));
    }
    const QByteArray bytes = file.read(MAX_THEME_BYTES + 1);
    if (file.error() != QFileDevice::NoError) {
        return std::unexpected(QString("Can't read the file: %1").arg(file.errorString()));
    }
    if (bytes.size() > MAX_THEME_BYTES) {
        return std::unexpected(QString("The file is too big to be a theme."));
    }
    return bytes;
}

expected<void, QString> copy_theme_file(const QString& source, const QString& target) {
    const auto bytes = read_small_file(source);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly) || file.write(*bytes) != bytes->size() || !file.commit()) {
        return std::unexpected(QString("Can't save the theme: %1").arg(file.errorString()));
    }
    return {};
}

// JSON: {"name", "window", "text", "base", "button", "accent", "header", "shadow",
// "disabledText", "effects": {"<effect ID>": colour}}; window and text are required.
static expected<ThemeSpec, QString> parse_json(const QByteArray& bytes,
                                               const QStringList& effect_ids) {
    QJsonParseError parse_error;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parse_error);
    if (!doc.isObject()) {
        return std::unexpected(parse_error.error != QJsonParseError::NoError
                                   ? QString("Not valid JSON: %1 (at byte %2).")
                                         .arg(parse_error.errorString())
                                         .arg(parse_error.offset)
                                   : QString("Not a theme: the file should hold one JSON object."));
    }
    const QJsonObject obj = doc.object();
    ThemeSpec spec;
    spec.name = obj.value("name").toString();

    const std::pair<const char*, QColor*> roles[] = {
        {"window", &spec.window}, {"text", &spec.text},
        {"base", &spec.base},     {"button", &spec.button},
        {"accent", &spec.accent}, {"header", &spec.header},
        {"shadow", &spec.shadow}, {"disabledText", &spec.disabled_text},
    };
    for (const auto& [key, color] : roles) {
        if (!obj.contains(key)) {
            continue;
        }
        *color = QColor(obj.value(key).toString());
        if (!color->isValid()) {
            return std::unexpected(
                QString("\"%1\" isn't a colour (use \"#rrggbb\").").arg(QLatin1String(key)));
        }
    }
    for (const char* key : {"window", "text"}) {
        if (!obj.contains(key)) {
            return std::unexpected(
                QString("\"%1\" is missing; a theme needs at least \"window\" and \"text\".")
                    .arg(QLatin1String(key)));
        }
    }
    for (const QString& key : obj.keys()) {
        const bool known = key == "name" || key == "effects" ||
                           std::any_of(std::begin(roles), std::end(roles), [&](const auto& r) {
                               return key == QLatin1String(r.first);
                           });
        if (!known) {
            return std::unexpected(QString("Unknown key \"%1\".").arg(key));
        }
    }

    if (obj.contains("effects") && !obj.value("effects").isObject()) {
        return std::unexpected(QString("\"effects\" should be an object of effect colours."));
    }
    const QJsonObject effects = obj.value("effects").toObject();
    for (auto it = effects.begin(); it != effects.end(); ++it) {
        if (!effect_ids.contains(it.key())) {
            return std::unexpected(
                QString("\"effects\" names an unknown effect \"%1\" (known: %2).")
                    .arg(it.key(), effect_ids.join(", ")));
        }
        const QColor color(it.value().toString());
        if (!color.isValid()) {
            return std::unexpected(
                QString("The \"%1\" effect colour isn't a colour (use \"#rrggbb\").")
                    .arg(it.key()));
        }
        spec.effects.insert(it.key(), color);
    }
    return spec;
}

// KDE colour scheme: INI groups, colours as "r,g,b". Window background and foreground
// are required; View, Button, Selection and Header backgrounds fill base, button, accent
// and header.
static expected<ThemeSpec, QString> parse_kde_colors(const QByteArray& bytes) {
    QHash<QString, QString> values; // "Group/Key" → value
    QString group;
    for (const QByteArray& raw : bytes.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(';')) {
            continue;
        }
        if (line.startsWith('[') && line.endsWith(']')) {
            group = line.mid(1, line.size() - 2);
            continue;
        }
        const qsizetype eq = line.indexOf('=');
        if (eq > 0) {
            values.insert(group + '/' + line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
        }
    }

    ThemeSpec spec;
    spec.name = values.value("General/Name");
    const std::pair<const char*, QColor*> roles[] = {
        {"Colors:Window/BackgroundNormal", &spec.window},
        {"Colors:Window/ForegroundNormal", &spec.text},
        {"Colors:View/BackgroundNormal", &spec.base},
        {"Colors:Button/BackgroundNormal", &spec.button},
        {"Colors:Selection/BackgroundNormal", &spec.accent},
        {"Colors:Header/BackgroundNormal", &spec.header},
    };
    for (const auto& [key, color] : roles) {
        const QString value = values.value(key);
        if (value.isEmpty()) {
            continue;
        }
        const QStringList parts = value.split(',');
        bool ok = parts.size() == 3 || parts.size() == 4;
        int rgb[3] = {};
        for (int i = 0; ok && i < 3; ++i) {
            rgb[i] = parts[i].trimmed().toInt(&ok);
            ok = ok && rgb[i] >= 0 && rgb[i] <= 255;
        }
        if (!ok) {
            return std::unexpected(
                QString("%1 isn't a colour (\"r,g,b\"): \"%2\".").arg(QLatin1String(key), value));
        }
        *color = QColor(rgb[0], rgb[1], rgb[2]);
    }
    if (!spec.window.isValid() || !spec.text.isValid()) {
        return std::unexpected(QString("Not a KDE colour scheme: [Colors:Window] needs "
                                       "BackgroundNormal and ForegroundNormal."));
    }
    return spec;
}

expected<ThemeSpec, QString> load_theme_file(const QString& path, const QStringList& effect_ids) {
    const auto bytes = read_small_file(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    const bool kde = path.endsWith(".colors", Qt::CaseInsensitive);
    auto spec = kde ? parse_kde_colors(*bytes) : parse_json(*bytes, effect_ids);
    if (!spec) {
        return spec;
    }
    if (spec->name.trimmed().isEmpty()) {
        spec->name = QFileInfo(path).completeBaseName();
    }
    fill_defaults(*spec);
    return spec;
}

expected<ThemeSpec, QString> load_kde_colors(const QString& path) {
    const auto bytes = read_small_file(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    auto spec = parse_kde_colors(*bytes);
    if (spec) {
        fill_defaults(*spec);
    }
    return spec;
}

QString themes_folder() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/themes";
}

QList<ThemeFileEntry> list_theme_files(const QStringList& effect_ids) {
    QList<ThemeFileEntry> entries;
    const QDir dir(themes_folder());
    for (const QFileInfo& info :
         dir.entryInfoList({"*.json", "*.colors"}, QDir::Files | QDir::Readable)) {
        const auto spec = load_theme_file(info.filePath(), effect_ids);
        entries.append({info.filePath(), spec ? spec->name : info.fileName(),
                        spec ? QString() : spec.error()});
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return entries;
}

} // namespace namx
