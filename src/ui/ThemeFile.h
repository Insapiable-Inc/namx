#pragma once

#include <QColor>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "Common.h"

namespace namx
{

// A user theme: the base colours Theme derives every chrome tone from (like it does with
// the OS palette or Breeze), plus optional per-effect accents. Read from our JSON format
// (`.json`) or a KDE colour scheme (`.colors`); BUILD.md → File formats has both.
struct ThemeSpec
{
    QString name;
    QColor window; // main window background
    QColor text;
    QColor base; // lists, fields
    QColor button; // effect cards, buttons
    QColor accent; // selections, the app accent
    QColor shadow;
    QColor disabled_text;
    QHash<QString, QColor> effects; // effect ID → accent; missing ones keep ours
};

// a + (b − a) · t, per channel.
QColor mix_colors(const QColor& a, const QColor& b, double t);

// Reads a theme file. `effect_ids` are the IDs an `effects` entry may name; anything
// else is an error, so a typo doesn't fail silently.
expected<ThemeSpec, QString> load_theme_file(const QString& path, const QStringList& effect_ids);

// Where user themes live (`<config>/themes`); not created here.
QString themes_folder();

// A file in the themes folder: its theme's name, or why it can't be used.
struct ThemeFileEntry
{
    QString path;
    QString name; // the file name when the theme has none or doesn't load
    QString error; // empty = usable
};
// Every .json / .colors file in the themes folder, sorted by name.
QList<ThemeFileEntry> list_theme_files(const QStringList& effect_ids);

} // namespace namx
