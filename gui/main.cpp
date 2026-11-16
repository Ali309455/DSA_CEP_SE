#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include "Icons.h"
#include "MainWindow.h"
#include "WinTheme.h"

// DevPad: usage  devpad [file-or-folder ...]
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("DevPad");
    QApplication::setOrganizationName("DevPad");
    QApplication::setWindowIcon(Icons::app());
    installDarkTitleBars();                            // dialogs get the dark Windows title bar too

    MainWindow w;
    bool any = false;
    for (int i = 1; i < argc; ++i) {
        const QFileInfo fi(QString::fromLocal8Bit(argv[i]));
        if (fi.isDir()) { w.setWorkspaceRoot(fi.absoluteFilePath()); w.addRecentFolder(fi.absoluteFilePath()); any = true; }
        else if (fi.isFile()) { w.openPath(fi.absoluteFilePath()); any = true; }
    }
    if (!any) w.restoreSession();
    w.show();
    return app.exec();
}
