#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include "Icons.h"
#include "MainWindow.h"
#include "Theme.h"
#include "WinTheme.h"

// DevPad: usage  devpad [file-or-folder ...]
//
// For checking the layout without clicking around (developers): with DEVPAD_SNAPSHOT=out.png set,
// DevPad uses its own throw-away settings, opens the arguments, optionally switches to
// DEVPAD_THEME (a theme id) and shows DEVPAD_PANELS (any of: terminal, problems, outline, history),
// saves a picture of the window to out.png after a moment and quits.
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const QString snapshot = qEnvironmentVariable("DEVPAD_SNAPSHOT");
    QApplication::setApplicationName(snapshot.isEmpty() ? "DevPad" : "DevPadSnapshot");
    QApplication::setOrganizationName(snapshot.isEmpty() ? "DevPad" : "DevPadSnapshot");
    QApplication::setWindowIcon(Icons::app());
    installDarkTitleBars();                            // dialogs get the dark Windows title bar too

    MainWindow w;
    bool any = false, askTrust = false;
    for (int i = 1; i < argc; ++i) {
        const QFileInfo fi(QString::fromLocal8Bit(argv[i]));
        if (fi.isDir()) { w.openFolder(fi.absoluteFilePath(), false); any = true; askTrust = true; }
        else if (fi.isFile()) { w.openPath(fi.absoluteFilePath()); any = true; }
    }
    if (!any && snapshot.isEmpty()) w.restoreSession();
    if (!snapshot.isEmpty()) {
        w.resize(1280, 800);
        const QString theme = qEnvironmentVariable("DEVPAD_THEME");
        if (!theme.isEmpty()) w.setTheme(theme);
        const QString panels = qEnvironmentVariable("DEVPAD_PANELS");
        if (panels.contains("terminal")) w.showTerminal(true);
        if (panels.contains("problems")) w.showProblems(true);
        if (panels.contains("outline")) w.showOutline(true);
        if (panels.contains("history")) w.showHistory(true);
        QTimer::singleShot(1500, &w, [&w, snapshot]() {
            w.grab().save(snapshot);
            QApplication::quit();
        });
    }
    w.show();
    // a folder from the command line that is not trusted: ask once the window is up (Restricted Mode until then)
    if (askTrust && snapshot.isEmpty() && w.restricted())
        QTimer::singleShot(0, &w, [&w]() { w.askTrust(w.workspaceRoot()); });
    return app.exec();
}
