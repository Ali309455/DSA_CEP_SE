// Theme.h - DevPad's look: one palette for every widget, the style sheet and the fonts.
//
// Dark "modern" theme: neutral chrome (#181818) around a slightly lighter editor (#1f1f1f),
// thin borders, rounded inputs and menus, one blue accent taken from the app icon.
#pragma once
#include <QColor>
#include <QFont>
#include <QString>

namespace Theme {

// chrome and surfaces
inline const QColor Chrome(0x18, 0x18, 0x18);        // activity bar, sidebar, tab strip, status bar
inline const QColor Editor(0x1f, 0x1f, 0x1f);        // editor and welcome page
inline const QColor Surface(0x2b, 0x2b, 0x2b);       // inputs, popups, cards
inline const QColor Border(0x2b, 0x2b, 0x2b);
inline const QColor Accent(0x4f, 0x8c, 0xff);
inline const QColor Text(0xcc, 0xcc, 0xcc);
inline const QColor Muted(0x9d, 0x9d, 0x9d);
inline const QColor Dim(0x6e, 0x6e, 0x6e);

// editor
inline const QColor CurrentLine(0x26, 0x26, 0x26);
inline const QColor Selection(0x26, 0x4f, 0x78);
inline const QColor FindMatch(0x62, 0x4a, 0x1c);
inline const QColor GutterNumber(0x6e, 0x76, 0x81);
inline const QColor GutterCurrent(0xcc, 0xcc, 0xcc);
inline const QColor Caret(0x6b, 0x9f, 0xff);
inline const QColor IndentGuide(0x33, 0x33, 0x33);

// syntax colours, in the order of the tokenizer's classes:
// text, keyword, type, string, number, comment, preprocessor
inline const QColor Syntax[] = {
    QColor(0xd4, 0xd4, 0xd4), QColor(0xc6, 0x78, 0xdd), QColor(0xe5, 0xc0, 0x7b), QColor(0x98, 0xc3, 0x79),
    QColor(0xd1, 0x9a, 0x66), QColor(0x7f, 0x84, 0x8e), QColor(0x56, 0xb6, 0xc2)};

// Fonts: the system UI font (Segoe UI on Windows) and a good monospace font that exists here
// (Cascadia Code / Mono on Windows 11, then JetBrains Mono, Fira Code, Consolas, ...).
QFont uiFont();
QFont codeFont(int pointSize);

// Palette, style sheet and font for the whole application.
void apply();
QString styleSheet();

}  // namespace Theme
