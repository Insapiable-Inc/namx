#include <QApplication>
#include <QSettings>
#include <QtGlobal>

#include "MainWindow.h"
#include "Theme.h"

// The app was registered with QSettings as "NAM Linux" before v1.0 — a misnomer
// that also baked "Linux" into the Windows registry path. Copy that store into
// the "NAM Player" one exactly once, so upgrading keeps saved port connections,
// device IDs, session state and window geometry. The legacy store is left in
// place (harmless, and a downgrade still finds it).
static void migrateLegacySettings()
{
    QSettings current; // org/app from QCoreApplication → the new location
    if (!current.allKeys().isEmpty())
        return;

    QSettings legacy(QStringLiteral("pichulero.cl"), QStringLiteral("NAM Linux"));
    const QStringList keys = legacy.allKeys();
    if (keys.isEmpty())
        return;

    for (const QString& key : keys)
        current.setValue(key, legacy.value(key));
    current.sync();
    qInfo("[NAM Player] Migrated %lld settings keys from the legacy \"NAM Linux\" store",
          static_cast<long long>(keys.size()));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    // Application name drives the QSettings path — keep in sync with migrateLegacySettings().
    app.setApplicationName("NAM Player");
    app.setApplicationDisplayName("NAM Player");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("pichulero.cl");

    migrateLegacySettings();
    Theme::apply(app);

    MainWindow w;
    w.show();
    return app.exec();
}
