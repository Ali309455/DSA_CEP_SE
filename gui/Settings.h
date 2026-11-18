// Settings.h - the user's preferences (phase 5, GUI refining).
//
// One object for the whole application. The settings window changes it, every widget that
// cares (CodeView, TerminalPanel, the theme) listens to changed() and updates itself, and it is
// stored with QSettings so it survives a restart. Defaults are in one place (Values).
#pragma once
#include <QObject>
#include <QString>

class AppSettings : public QObject {
    Q_OBJECT
public:
    struct Values {
        // editor
        int fontSize = 11;                 // points (Ctrl+Wheel zoom changes this too)
        QString fontFamily;                // empty: the best monospace font that is installed
        int tabWidth = 4;                  // columns per tab stop: 2, 4 or 8
        bool autoPairs = true;             // typing ( [ { " ' ` adds the closer
        bool wrapSelection = true;         // typing an opener with text selected wraps the text
        bool autoComplete = true;          // suggestions pop up while typing
        int cursorStyle = 0;               // 0 line, 1 block, 2 underline
        bool cursorBlink = true;
        // appearance
        QString theme = "midnight";        // Theme::schemeIds(): midnight, daylight, nord, solarized-light
        QString accent = "blue";           // key into accentColor()
        bool lineNumbers = true;
        bool indentGuides = true;
        bool highlightLine = true;
        bool matchBrackets = true;
        bool statusBar = true;
        // terminal
        int terminalFontSize = 10;
        QString terminalShell;             // empty: cmd on Windows, $SHELL elsewhere
        int terminalScrollback = 5000;     // lines kept
        // general
        bool restoreSession = true;

        bool operator==(const Values& o) const;
        bool operator!=(const Values& o) const { return !(*this == o); }
    };

    static AppSettings& instance();

    const Values& values() const { return v_; }
    // Replace everything, store it, and tell the listeners (only if something really changed).
    void apply(const Values& v);
    void resetToDefaults() { apply(Values()); }
    void reload();                         // read QSettings again (tests, first use)

    // The accent colours the settings window offers: key, display name, colour.
    struct Accent { const char* key; const char* name; unsigned rgb; };
    static const Accent* accents(int* count);
    static unsigned accentRgb(const QString& key);

    // Values are clamped to what the rest of the program supports, so a hand-edited
    // settings file cannot produce a 2-pixel font or a tab width of 99.
    static Values sanitized(Values v);

signals:
    void changed();

private:
    AppSettings();
    void load();
    void save() const;
    Values v_;
};
