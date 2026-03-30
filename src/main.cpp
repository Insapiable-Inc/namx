#include <QApplication>
#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("NAMPlayer");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("NAM");
    app.setStyle("Fusion");

    MainWindow w;
    w.show();

    return app.exec();
}
