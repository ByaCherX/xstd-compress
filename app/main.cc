// XSTD Archive Manager — Qt6 GUI Application
// Entry point

#include <QApplication>
#include "mainwindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // Application metadata (used by QSettings, window title, etc.)
    QCoreApplication::setOrganizationName("xstd");
    QCoreApplication::setApplicationName("XSTD Archive Manager");
    QCoreApplication::setApplicationVersion("1.0.0");

    MainWindow window;
    window.resize(960, 620);
    window.show();

    return app.exec();
}
