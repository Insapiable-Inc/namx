#include <QApplication>
#include <QStyleFactory>
#include "MainWindow.h"
#include "MainWindowAlt.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("NAM Linux");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("pichulero.cl");

    // Prefer Breeze for native KDE/Plasma look; fall back to Fusion.
    if (!QStyleFactory::keys().contains("Breeze", Qt::CaseInsensitive))
        app.setStyle("Fusion");
    else
        app.setStyle("Breeze");

    // Pass --alt to use the column-based alternative layout.
    const QStringList args = QCoreApplication::arguments();
    if (args.contains("--alt")) {
        MainWindowAlt w;
        w.show();
        return app.exec();
    }

    MainWindow w;
    w.show();
    return app.exec();
}
