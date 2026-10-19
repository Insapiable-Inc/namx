// Headless test of the theme file reader (ThemeFile.h): our JSON and KDE's .colors, a
// complete file, a minimal one (the defaults), and the broken files a user is likely to
// write — each must load or say why, never crash or half-apply.
//
// The files are written below into a QTemporaryDir this test creates.

#include "ThemeFile.h"

#include <cstdio>
#include <cstdlib>

#include <QFile>
#include <QTemporaryDir>

#include "Common.h"

namespace namx
{

static int failures = 0;
#define CHECK(cond, msg) \
    do { \
        if (cond) \
            std::printf("  ok   %s\n", msg); \
        else { \
            std::printf("  FAIL %s  (%s:%d)\n", msg, __FILE__, __LINE__); \
            ++failures; \
        } \
    } while (0)

static const QStringList EFFECT_IDS = {"gate", "nam", "ir"};

static QString write_file(const QTemporaryDir& dir, const char* name, const char* text) {
    const QString path = dir.filePath(name);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(text);
    }
    return path;
}

// Within `steps` per channel: the derived tones only need to land near KDE's.
static bool near(const QColor& a, const char* b, int steps) {
    const QColor c(b);
    return std::abs(a.red() - c.red()) <= steps && std::abs(a.green() - c.green()) <= steps &&
           std::abs(a.blue() - c.blue()) <= steps;
}

static bool fails_with(const QTemporaryDir& dir, const char* name, const char* text,
                       const char* fragment) {
    const auto spec = load_theme_file(write_file(dir, name, text), EFFECT_IDS);
    if (spec) {
        return false;
    }
    std::printf("       → %s\n", qPrintable(spec.error()));
    return spec.error().contains(QLatin1String(fragment));
}

static void test_json(const QTemporaryDir& dir) {
    std::printf("JSON\n");
    const auto full = load_theme_file(write_file(dir, "full.json", R"({
        "name": "Stage", "window": "#101010", "text": "#f0f0f0", "base": "#000000",
        "button": "#202020", "accent": "#ff8800", "header": "#181818", "shadow": "#050505",
        "disabledText": "#707070", "effects": {"nam": "#00ff00", "ir": "red"}
    })"),
                                      EFFECT_IDS);
    CHECK(full.has_value(), "a complete theme loads");
    if (full) {
        CHECK(full->name == "Stage", "its name");
        CHECK(full->window == QColor("#101010") && full->text == QColor("#f0f0f0") &&
                  full->base == QColor("#000000") && full->button == QColor("#202020") &&
                  full->accent == QColor("#ff8800") && full->header == QColor("#181818") &&
                  full->shadow == QColor("#050505") && full->disabled_text == QColor("#707070"),
              "every colour as written");
        CHECK(full->effects.size() == 2 && full->effects.value("nam") == QColor("#00ff00") &&
                  full->effects.value("ir") == QColor(Qt::red),
              "effect accents, hex and named");
    }

    const auto minimal = load_theme_file(
        write_file(dir, "Plain Light.json", R"({"window": "#eff0f1", "text": "#232629"})"),
        EFFECT_IDS);
    CHECK(minimal.has_value(), "window + text is enough");
    if (minimal) {
        CHECK(minimal->name == "Plain Light", "no name: the file name");
        CHECK(minimal->base == minimal->window && minimal->button == minimal->window &&
                  minimal->header == minimal->window,
              "base, button and header default to the window");
        CHECK(minimal->accent == QColor("#3daee9"), "accent defaults to Breeze blue");
        CHECK(near(minimal->shadow, "#474a4c", 4) && near(minimal->disabled_text, "#a0a1a3", 10),
              "shadow and disabled text near Breeze Light's");
        CHECK(minimal->effects.isEmpty(), "no effect accents");
    }

    CHECK(fails_with(dir, "nojson.json", "{\"window\": ", "Not valid JSON"), "truncated JSON");
    CHECK(fails_with(dir, "array.json", "[1, 2]", "one JSON object"), "not an object");
    CHECK(fails_with(dir, "notext.json", R"({"window": "#000000"})", "\"text\" is missing"),
          "text missing");
    CHECK(fails_with(dir, "badcolor.json", R"({"window": "#00000g", "text": "#fff"})",
                     "\"window\" isn't a colour"),
          "an invalid colour");
    CHECK(fails_with(dir, "typo.json", R"({"window": "#000", "text": "#fff", "acent": "#f00"})",
                     "Unknown key \"acent\""),
          "a misspelt key");
    CHECK(fails_with(dir, "fx.json",
                     R"({"window": "#000", "text": "#fff", "effects": {"gat": "#f00"}})",
                     "unknown effect \"gat\""),
          "an unknown effect ID");
    CHECK(fails_with(dir, "fx-array.json", R"({"window": "#000", "text": "#fff", "effects": []})",
                     "\"effects\" should be an object"),
          "malformed effect overrides are rejected instead of silently ignored");
}

static void test_copy(const QTemporaryDir& dir) {
    std::printf("Theme replacement\n");
    const QString target = write_file(dir, "installed.json", "original theme");
    auto contents = [&] {
        QFile file(target);
        if (!file.open(QIODevice::ReadOnly)) {
            return QByteArray();
        }
        return file.readAll();
    };
    CHECK(!copy_theme_file(dir.filePath("missing-source.json"), target),
          "missing import source fails");
    CHECK(contents() == "original theme", "failed import preserves the installed theme");
    const QString source = write_file(dir, "replacement.json", "replacement theme");
    CHECK(copy_theme_file(source, target).has_value(), "replace an existing theme");
    CHECK(contents() == "replacement theme", "replacement bytes are saved");
    CHECK(!copy_theme_file(source, dir.path()), "unwritable destination fails");
    CHECK(copy_theme_file(target, target).has_value(), "copying onto itself preserves the file");
    CHECK(contents() == "replacement theme", "same-file copy keeps the contents");
}

static void test_kde(const QTemporaryDir& dir) {
    std::printf("KDE .colors\n");
    // Breeze Dark's relevant groups, as /usr/share/color-schemes/BreezeDark.colors has them.
    const auto breeze = load_theme_file(write_file(dir, "BreezeDark.colors", R"(
[ColorEffects:Disabled]
Color=56,56,56

[Colors:Button]
BackgroundNormal=41,44,48
ForegroundNormal=252,252,252

[Colors:Header]
BackgroundNormal=41,44,48

[Colors:Header][Inactive]
BackgroundNormal=32,35,38

[Colors:Selection]
BackgroundNormal=61,174,233

[Colors:View]
BackgroundNormal=20,22,24

[Colors:Window]
BackgroundNormal=32,35,38
ForegroundNormal=252,252,252

[General]
ColorScheme=BreezeDark
Name=Breeze Dark
Name[es]=Brisa oscuro
)"),
                                        EFFECT_IDS);
    CHECK(breeze.has_value(), "Breeze Dark loads");
    if (breeze) {
        CHECK(breeze->name == "Breeze Dark", "its Name (not a translation)");
        CHECK(breeze->window == QColor("#202326") && breeze->text == QColor("#fcfcfc") &&
                  breeze->base == QColor("#141618") && breeze->button == QColor("#292c30") &&
                  breeze->accent == QColor("#3daee9") && breeze->header == QColor("#292c30"),
              "window, text, view, button, selection, header (the active one)");
        // What Qt's KDE platform theme reports under Breeze Dark (2026-10-09).
        CHECK(near(breeze->shadow, "#0e0f10", 4) && near(breeze->disabled_text, "#686a6c", 10),
              "shadow and disabled text near Qt's");
    }

    CHECK(fails_with(dir, "empty.colors", "[General]\nName=Nothing\n", "[Colors:Window]"),
          "no window colours");
    CHECK(fails_with(dir, "bad.colors",
                     "[Colors:Window]\nBackgroundNormal=32,35\n"
                     "ForegroundNormal=252,252,252\n",
                     "isn't a colour"),
          "a colour with two channels");
    CHECK(fails_with(dir, "range.colors",
                     "[Colors:Window]\nBackgroundNormal=32,35,300\n"
                     "ForegroundNormal=252,252,252\n",
                     "isn't a colour"),
          "a channel above 255");
}

static void test_missing(const QTemporaryDir& dir) {
    std::printf("Missing file\n");
    const auto missing = load_theme_file(dir.filePath("missing.json"), EFFECT_IDS);
    CHECK(!missing && missing.error().startsWith("Can't open"), "says it can't open it");
}

} // namespace namx

int main() {
    const QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("FAIL no temporary directory\n");
        return 1;
    }
    namx::test_json(dir);
    namx::test_kde(dir);
    namx::test_missing(dir);
    namx::test_copy(dir);
    std::printf(namx::failures ? "%d FAILED\n" : "all passed\n", namx::failures);
    return namx::failures ? 1 : 0;
}
