#include <QApplication>
#include "MainWindow.h"
#include "Theme.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("NAM Linux");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("pichulero.cl");

    Theme::apply(app);

    MainWindow w;
    w.show();
    return app.exec();
}
