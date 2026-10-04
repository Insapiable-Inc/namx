#include <cstdio>
#include <cstring>

#include <QApplication>
#include <QElapsedTimer>
#include <QIcon>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QtGlobal>

#include "Common.h"
#include "MainWindow.h"
#include "Theme.h"

// Last: <windows.h> defines macros (ABSOLUTE, RELATIVE, …) that break any of our headers
// included after it — MidiMapper::Mode's enumerators among them.
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace namx
{

#ifndef NAM_VERSION
#define NAM_VERSION "0.0.0"
#endif

// The app's QSettings store lived under the "pichulero.cl" organization until
// 2026-10-04 (now "Insapiable"), as "namx", before that "NAM Player" (until the NAM-X
// rename, 2026-10-03) and "NAM Linux". On the first start of an empty store, copy the
// newest legacy store that has anything in it, exactly once, so upgrading keeps saved
// port connections, device IDs, the library, MIDI mappings, session state and window
// geometry. Legacy stores are left in place (harmless, and a downgrade still finds them).
static void migrate_legacy_settings() {
    QSettings current; // org/app from QCoreApplication → the new location
    if (!current.allKeys().isEmpty()) {
        return;
    }

    for (const char* name : {"namx", "NAM Player", "NAM Linux"}) {
        QSettings legacy(QStringLiteral("pichulero.cl"), QString::fromUtf8(name));
        const QStringList keys = legacy.allKeys();
        if (keys.isEmpty()) {
            continue;
        }
        for (const QString& key : keys) {
            current.setValue(key, legacy.value(key));
        }
        current.sync();
        qInfo("[NAM-X] Migrated %lld settings keys from the legacy pichulero.cl/\"%s\" store",
              static_cast<long long>(keys.size()), name);
        return;
    }
}

// Self-test end: report peak memory and quit with success.
static void finish_self_test() {
#ifdef Q_OS_LINUX
    // Peak resident set (VmHWM) after the library scan and audio start.
    if (FILE* f = std::fopen("/proc/self/status", "r")) {
        char line[256];
        while (std::fgets(line, sizeof line, f)) {
            if (!std::strncmp(line, "VmHWM:", 6)) {
                std::printf("self-test: peak RSS %s", line + 6);
            }
        }
        std::fclose(f);
    }
#endif
    std::fflush(stdout);
    QCoreApplication::exit(0);
}

int run(int argc, char* argv[]) {
    QElapsedTimer startup; // cold-start budget (Phase 14): window up in < 1 s
    startup.start();
    // --version / --help answer before any GUI is created, so they work on a
    // headless box (and in the CI smoke test) without a display.
#ifdef Q_OS_WIN
    // A GUI-subsystem executable has no console: attach to the parent's so the two
    // flags print there (audit 2026-10-04, Fable item 49).
    if (argc > 1 && AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* unused = nullptr;
        freopen_s(&unused, "CONOUT$", "w", stdout);
        freopen_s(&unused, "CONOUT$", "w", stderr);
    }
#endif
    bool self_test = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--version") || !std::strcmp(argv[i], "-v")) {
            std::printf("NAM-X %s\n", NAM_VERSION);
            return 0;
        }
        if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            std::printf(
                "Usage: namx [options] [file...]\n"
                "  file                 .nam model, .wav impulse response or .nampreset to open\n"
                "  -v, --version        print the version and exit\n"
                "  --self-test-and-exit build the window, scan the library, print the\n"
                "                       startup time and peak RSS, exit 0 after 1.5 s\n"
                "  -h, --help           this text\n");
            return 0;
        }
        if (!std::strcmp(argv[i], "--self-test-and-exit")) {
            self_test = true;
        }
    }

#ifdef Q_OS_LINUX
    // Route the QFileDialog static getters through the XDG Desktop Portal, so
    // file dialogs are the desktop's own (Breeze on Plasma) instead of an
    // in-process Fusion-styled widget. The portal platform theme wraps the
    // session's native theme for everything else, and Qt falls back to the
    // widget dialog where no portal service is running. A user-chosen theme
    // (qt6ct etc.) is left alone; "kde" is what the wrapper resolves anyway.
    const QByteArray theme = qgetenv("QT_QPA_PLATFORMTHEME");
    if (theme.isEmpty() || theme == "kde") {
        qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
    }
#endif

    QApplication app(argc, argv);
    // Application name drives the QSettings path (…/Insapiable/namx.conf) — keep in
    // sync with migrate_legacy_settings(). The display name is what windows show.
    app.setApplicationName("namx");
    app.setApplicationDisplayName("NAM-X");
    app.setApplicationVersion(NAM_VERSION);
    app.setOrganizationName("Insapiable");
    app.setDesktopFileName("io.github.Insapiable_Inc.namx");
    app.setWindowIcon(QIcon(":/icons/namx-256.png"));

    // Positional arguments after QApplication has taken Qt's own options
    // (-platform …) out; an option this app doesn't know is an error, not a file
    // (audit 2026-10-04, Fable item 10).
    QStringList files;
    const QStringList args = app.arguments();
    for (qsizetype i = 1; i < args.size(); ++i) {
        const QString& a = args[i];
        if (a == "--self-test-and-exit") {
            continue;
        }
        if (a.startsWith('-') && a.size() > 1) {
            std::fprintf(stderr, "namx: unknown option '%s' (try --help)\n", qPrintable(a));
            return 2;
        }
        files << a;
    }

    migrate_legacy_settings();
    Theme::apply(app);

    MainWindow w;
    Theme::apply_to(w); // window-scoped so OS dialogs keep the system look
    w.show();
    if (!files.isEmpty()) {
        w.open_paths(files);
    }
    if (self_test) {
        // First event-loop pass after show(): the window has been laid out
        // and its first frame requested — what the user perceives as "up".
        QTimer::singleShot(0, &app, [startup] {
            std::printf("self-test: window shown after %lld ms\n",
                        static_cast<long long>(startup.elapsed()));
        });
        // Every rack control must be in the registry, so presets save it.
        const QStringList missing = w.controls_missing_from_presets();
        for (const QString& m : missing) {
            std::printf("self-test: not saved in presets: %s\n", qPrintable(m));
        }
        if (!missing.isEmpty()) {
            std::fflush(stdout);
            return 1;
        }
        QTimer::singleShot(1500, &app, &finish_self_test);
    } else {
        // After the window is up, so the question has a parent on screen.
        QTimer::singleShot(0, &w, &MainWindow::offer_desktop_integration);
    }
    return app.exec();
}

} // namespace namx

int main(int argc, char* argv[]) {
    return namx::run(argc, argv);
}
