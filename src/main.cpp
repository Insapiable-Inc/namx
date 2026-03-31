#include <QApplication>
#include <QStyleFactory>
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

    MainWindowAlt w;
    w.show();
    return app.exec();
}
