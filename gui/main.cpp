// DevPad - Qt front end (phase 5). Usage: devpad [file.cpp]
#include <QApplication>
#include <QFileInfo>
#include "MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("DSA-CEP");
    QCoreApplication::setApplicationName("DevPad");
    MainWindow w;
    const QStringList args = QCoreApplication::arguments();
    if (args.size() > 1) w.openPath(args.at(1));
    w.show();
    return app.exec();
}
