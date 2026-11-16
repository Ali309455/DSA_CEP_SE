// WinTheme.h - dark window title bars on Windows 10 / 11 (the title bar is drawn by Windows,
// not by Qt, so the dark theme needs one call per window). Does nothing on other systems.
#pragma once

class QWidget;

// Dark title bar for this window.
void enableDarkTitleBar(QWidget* window);

// Dark title bars for every window the application shows from now on (dialogs, message boxes).
void installDarkTitleBars();
