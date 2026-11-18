#include "WinTheme.h"

#include <QApplication>
#include <QEvent>
#include <QWidget>
#include "Theme.h"

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
    const BOOL on = Theme::isDark() ? TRUE : FALSE;       // a dark title bar for the dark themes, a light one for the light themes
    // DWMWA_USE_IMMERSIVE_DARK_MODE: 20 on Windows 10 20H1 and later, 19 on older Windows 10 builds
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &on, sizeof(on)))) DwmSetWindowAttribute(hwnd, 19, &on, sizeof(on));
    const COLORREF caption = RGB(Theme::Chrome.red(), Theme::Chrome.green(), Theme::Chrome.blue());   // DWMWA_CAPTION_COLOR (Windows 11), ignored elsewhere
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
    const COLORREF text = RGB(Theme::Text.red(), Theme::Text.green(), Theme::Text.blue());            // DWMWA_TEXT_COLOR (Windows 11)
    DwmSetWindowAttribute(hwnd, 36, &text, sizeof(text));
}

void refreshTitleBars() {
    for (QWidget* w : QApplication::topLevelWidgets())
        if (w->isWindow() && w->property("devpadDarkTitle").toBool()) enableDarkTitleBar(w);
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
void refreshTitleBars() {}

#endif
