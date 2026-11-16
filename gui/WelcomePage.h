// WelcomePage.h - what DevPad shows when no file is open (phase 5).
//
// Start actions (new file, open file, open folder, go to file), the recently used files and
// folders, and the most useful keyboard shortcuts. Every entry is a link; the window reacts to
// the signals.
#pragma once
#include <QStringList>
#include <QWidget>
#include <utility>
#include <vector>

class QLabel;
class QPushButton;
class QVBoxLayout;

class WelcomePage : public QWidget {
    Q_OBJECT
public:
    explicit WelcomePage(QWidget* parent = nullptr);

    // Lists shown under "Recent" (most recent first). Entries that no longer exist are skipped.
    void setRecent(const QStringList& files, const QStringList& folders);
    void setFolderOpen(bool open);               // "Go to File…" only makes sense with a folder
    QStringList shownRecent() const { return shown_; }

    // Follow one of the page's links: "new", "open", "folder", "quick", "keys",
    // "file:<path>" or "folder:<path>". The links call this; the tests do too.
    void activate(const QString& link);

    // The accent colour changed (Settings > Appearance): recolour icons and links.
    void refreshAccent();

signals:
    void newFileRequested();
    void openFileRequested();
    void openFolderRequested();
    void quickOpenRequested();
    void shortcutsRequested();
    void settingsRequested();
    void terminalRequested();
    void recentFileRequested(const QString& path);
    void recentFolderRequested(const QString& path);

private:
    QPushButton* quick_ = nullptr;
    QLabel* keysLink_ = nullptr;
    QLabel* settingsLink_ = nullptr;
    QLabel* terminalLink_ = nullptr;
    void refreshLinks();
    QVBoxLayout* recentBox_ = nullptr;
    QStringList shown_;
    QStringList recentFiles_, recentFolders_;
    std::vector<std::pair<QLabel*, int>> cardIcons_;     // icon label and glyph, to recolour
    QLabel* link(const QString& html, QWidget* parent);
    QPushButton* card(const QString& link, int glyph, const QString& title, const QString& hint, QWidget* parent);
};
