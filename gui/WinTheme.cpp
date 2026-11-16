#include "WinTheme.h"

#include <QApplication>
#include <QEvent>
#include <QWidget>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>

void enableDarkTitleBar(QWidget* window) {
    if (!window) return;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL on = TRUE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE: 20 on Windows 10 20H1 and later, 19 on older Windows 10 builds
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &on, sizeof(on)))) DwmSetWindowAttribute(hwnd, 19, &on, sizeof(on));
    const COLORREF caption = RGB(0x18, 0x18, 0x18);       // DWMWA_CAPTION_COLOR (Windows 11), ignored elsewhere
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
}

namespace {
class DarkTitleFilter : public QObject {
public:
    using QObject::QObject;
protected:
    bool eventFilter(QObject* obj, QEvent* e) override {
        if (e->type() == QEvent::Show) {
            QWidget* w = qobject_cast<QWidget*>(obj);
            if (w && w->isWindow() && !w->property("devpadDarkTitle").toBool()) {
                w->setProperty("devpadDarkTitle", true);
                enableDarkTitleBar(w);
            }
        }
        return false;
    }
};
}  // namespace

void installDarkTitleBars() {
    static DarkTitleFilter* filter = nullptr;
    if (!filter) {
        filter = new DarkTitleFilter(qApp);
        qApp->installEventFilter(filter);
    }
}

#else

void enableDarkTitleBar(QWidget*) {}
void installDarkTitleBars() {}

#endif
