#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include "MainWindow.h"

// DevPad: usage  devpad [file-or-folder ...]
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("DevPad");
    QApplication::setOrganizationName("DevPad");

    MainWindow w;
    bool any = false;
    for (int i = 1; i < argc; ++i) {
        const QFileInfo fi(QString::fromLocal8Bit(argv[i]));
        if (fi.isDir()) { w.setWorkspaceRoot(fi.absoluteFilePath()); any = true; }
        else if (fi.isFile()) { w.openPath(fi.absoluteFilePath()); any = true; }
    }
    if (!any) w.restoreSession();
    w.show();
    return app.exec();
}
