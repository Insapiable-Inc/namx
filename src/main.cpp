#include <cstdio>
#include <cstring>

#include <QApplication>
#include <QElapsedTimer>
#include <QIcon>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QtGlobal>

#include "MainWindow.h"
#include "Theme.h"

#ifndef NAM_VERSION
#define NAM_VERSION "0.0.0"
#endif

// The app was registered with QSettings as "NAM Linux" before v1.0 — a misnomer
// that also baked "Linux" into the Windows registry path. Copy that store into
// the "NAM Player" one exactly once, so upgrading keeps saved port connections,
// device IDs, session state and window geometry. The legacy store is left in
// place (harmless, and a downgrade still finds it).
static void migrate_legacy_settings() {
    QSettings current; // org/app from QCoreApplication → the new location
    if (!current.allKeys().isEmpty()) {
        return;
    }

    QSettings legacy(QStringLiteral("pichulero.cl"), QStringLiteral("NAM Linux"));
    const QStringList keys = legacy.allKeys();
    if (keys.isEmpty()) {
        return;
    }

    for (const QString& key : keys) {
        current.setValue(key, legacy.value(key));
    }
    current.sync();
    qInfo("[NAM Player] Migrated %lld settings keys from the legacy \"NAM Linux\" store",
          static_cast<long long>(keys.size()));
}

int main(int argc, char* argv[]) {
    QElapsedTimer startup; // cold-start budget (Phase 14): window up in < 1 s
    startup.start();
    // --version / --help answer before any GUI is created, so they work on a
    // headless box (and in the CI smoke test) without a display.
    bool self_test = false;
    QStringList files;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--version") || !std::strcmp(argv[i], "-v")) {
            std::printf("NAM Player %s\n", NAM_VERSION);
            return 0;
        }
        if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            std::printf(
                "Usage: NAMPlayer [options] [file...]\n"
                "  file                 .nam model, .wav impulse response or .nampreset to open\n"
                "  -v, --version        print the version and exit\n"
                "  --self-test-and-exit build the window, scan the library, print the\n"
                "                       startup time and peak RSS, exit 0 after 1.5 s\n"
                "  -h, --help           this text\n");
            return 0;
        }
        if (!std::strcmp(argv[i], "--self-test-and-exit")) {
            self_test = true;
            continue;
        }
        files << QString::fromLocal8Bit(argv[i]);
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
    // Application name drives the QSettings path — keep in sync with migrateLegacySettings().
    app.setApplicationName("NAM Player");
    app.setApplicationDisplayName("NAM Player");
    app.setApplicationVersion(NAM_VERSION);
    app.setOrganizationName("pichulero.cl");
    app.setDesktopFileName("cl.pichulero.NAMPlayer");
    app.setWindowIcon(QIcon(":/icons/NAMPlayer-256.png"));

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
        QTimer::singleShot(0, &app, [&startup] {
            std::printf("self-test: window shown after %lld ms\n",
                        static_cast<long long>(startup.elapsed()));
        });
        QTimer::singleShot(1500, &app, [] {
#ifdef Q_OS_LINUX
            // Peak resident set (VmHWM) after the library scan and audio start.
            if (FILE* f = std::fopen("/proc/self/status", "r")) {
                char line[256];
                while (std::fgets(line, sizeof line, f))
                    if (!std::strncmp(line, "VmHWM:", 6))
                        std::printf("self-test: peak RSS %s", line + 6);
                std::fclose(f);
            }
#endif
            std::fflush(stdout);
            QCoreApplication::exit(0);
        });
    }
    return app.exec();
}
