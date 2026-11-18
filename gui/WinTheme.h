// WinTheme.h - window title bars that match the theme on Windows 10 / 11 (the title bar is drawn
// by Windows, not by Qt, so the theme needs one call per window: dark for the dark themes, light for
// the light ones). Does nothing on other systems.
#pragma once

class QWidget;

// Title bar of this window in the colours of the current theme.
void enableDarkTitleBar(QWidget* window);

// Title bars for every window the application shows from now on (dialogs, message boxes).
void installDarkTitleBars();

// After a theme change: give every window that is already open the new title bar.
void refreshTitleBars();
