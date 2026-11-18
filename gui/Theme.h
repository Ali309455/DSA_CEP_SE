// Theme.h - DevPad's look: one palette for every widget, the style sheet and the fonts.
//
// Four built-in themes (the ones the first DevPad versions had):
//   Midnight        the dark "modern" theme: neutral chrome (#181818) around a slightly lighter editor
//   Daylight        light, GitHub-like
//   Nord            dark blue-grey
//   Solarized Light warm light
// The colours below are plain variables that setScheme() overwrites, so every widget that reads
// them (CodeView paints with them, the style sheet is built from them) follows a theme change.
// The accent colour is separate (Settings > Appearance > Accent colour) and works with every theme.
#pragma once
#include <QColor>
#include <QFont>
#include <QObject>
#include <QString>
#include <QStringList>

namespace Theme {

// chrome and surfaces
inline QColor Chrome(0x18, 0x18, 0x18);        // activity bar, sidebar, tab strip, status bar
inline QColor Editor(0x1f, 0x1f, 0x1f);        // editor and welcome page
inline QColor Surface(0x2b, 0x2b, 0x2b);       // inputs, popups, cards
inline QColor Border(0x2b, 0x2b, 0x2b);
inline QColor Accent(0x4f, 0x8c, 0xff);        // changed by setAccent() (Settings > Appearance)
inline QColor Text(0xcc, 0xcc, 0xcc);
inline QColor Muted(0x9d, 0x9d, 0x9d);
inline QColor Dim(0x6e, 0x6e, 0x6e);
inline QColor Heading(0xf0, 0xf0, 0xf0);       // titles and the selected tab

// controls and cards
inline QColor Raised(0x25, 0x25, 0x26);        // cards, tooltips, keyboard keys' background family
inline QColor CardBorder(0x30, 0x30, 0x30);
inline QColor InputBorder(0x3a, 0x3a, 0x3a);   // border of inputs, buttons, combo boxes
inline QColor Popup(0x1f, 0x1f, 0x1f);         // menus, combo lists, completion list
inline QColor PopupBorder(0x38, 0x38, 0x38);
inline QColor RowHover(0x2e, 0x2e, 0x2e);      // hover on menu-bar items, tool buttons, list rows
inline QColor Pressed(0x35, 0x35, 0x35);
inline QColor Button(0x2d, 0x2d, 0x2d);
inline QColor ButtonHover(0x36, 0x36, 0x36);
inline QColor Keycap(0x33, 0x33, 0x33);
inline QColor KeycapBorder(0x45, 0x45, 0x45);
inline QColor TreeHover(0x2a, 0x2d, 0x2e);

// editor
inline QColor CurrentLine(0x26, 0x26, 0x26);
inline QColor Selection(0x26, 0x4f, 0x78);
inline QColor FindMatch(0x62, 0x4a, 0x1c);
inline QColor GutterNumber(0x6e, 0x76, 0x81);
inline QColor GutterCurrent(0xcc, 0xcc, 0xcc);
inline QColor Caret(0x6b, 0x9f, 0xff);
inline QColor IndentGuide(0x33, 0x33, 0x33);

// syntax colours, in the order of the tokenizer's classes:
// text, keyword, type, string, number, comment, preprocessor
inline QColor Syntax[] = {
    QColor(0xd4, 0xd4, 0xd4), QColor(0xc6, 0x78, 0xdd), QColor(0xe5, 0xc0, 0x7b), QColor(0x98, 0xc3, 0x79),
    QColor(0xd1, 0x9a, 0x66), QColor(0x7f, 0x84, 0x8e), QColor(0x56, 0xb6, 0xc2)};

// status colours (errors, warnings, success) and line-icon colours
inline QColor Error(0xf1, 0x4c, 0x4c);
inline QColor Warning(0xcc, 0xa7, 0x00);
inline QColor Success(0x73, 0xc9, 0x91);
inline QColor IconNormal(0x9d, 0x9d, 0x9d);    // line icons at rest
inline QColor IconActive(0xe8, 0xe8, 0xe8);    // ... hovered or checked

// ---- the themes ----
struct SchemeInfo {
    QString id;       // what is stored in the settings ("midnight", "daylight", "nord", "solarized-light")
    QString name;     // what the user sees
    bool dark;
};
int schemeCount();
SchemeInfo schemeInfo(int index);
QStringList schemeIds();
bool isKnownScheme(const QString& id);
QString schemeName(const QString& id);
QString defaultScheme();                       // "midnight"
QString currentScheme();                       // id of the theme in use
bool isDark();                                 // is the theme in use a dark one?
// The theme the quick toggle switches to: Midnight <-> Daylight, Nord <-> Solarized Light.
QString toggledScheme(const QString& id);
// Use a theme: sets every colour above, rebuilds the application style sheet if it is already in place
// and tells notifier() listeners. An unknown id means Midnight.
void setScheme(const QString& id);

// Emits changed() after the theme or the accent colour changed (title bars, cached icons, ... listen).
class Notifier : public QObject {
    Q_OBJECT
signals:
    void changed();
};
Notifier& notifier();

// Fonts: the system UI font (Segoe UI on Windows) and a good monospace font that exists here
// (Cascadia Code / Mono on Windows 11, then JetBrains Mono, Fira Code, Consolas, ...).
QFont uiFont();
QFont codeFont(int pointSize, const QString& family = QString());   // family empty: the automatic choice

// Colours derived from the accent (hover, soft selection background, deep menu selection).
QColor accentHover();
QColor accentSoft();
QColor accentDeep();
QColor selectionText();                        // text colour on accentDeep() / accentSoft()
// Choose the accent colour and, if the application style sheet is already in place, rebuild it.
void setAccent(const QColor& c);
// Read the theme and the accent from the user's settings (call before building widgets). It also
// keeps the theme in step with the settings from then on (Settings > Appearance, the toggle).
void loadAccent();

// Palette, style sheet and font for the whole application.
void apply();
QString styleSheet();
// A small style sheet for the popup lists (completion list, Go to File): colours of the current theme.
QString popupListStyle();

}  // namespace Theme
