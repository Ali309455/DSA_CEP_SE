// MainWindow.h - DevPad's main window (phase 5, Qt Widgets).
//
// Tabs (one CodeView each), menus + keyboard shortcuts, find / replace bar, Quick Open
// (Ctrl+P), file manager sidebar (FileExplorer), rename file (F2), session restore. All text editing and all
// searching is done by the engine; this class wires it to the user interface.
#pragma once
#include <QMainWindow>
#include <QPointer>
#include <QString>
#include <vector>
#include "Search.h"

class CodeView;
class QTabWidget;
class QLineEdit;
class QCheckBox;
class QToolButton;
class QLabel;
class QDockWidget;
class QPushButton;
class FileExplorer;
class WelcomePage;
class QStackedWidget;
class QSplitter;
class TerminalPanel;
class SettingsDialog;
class ShortcutsDialog;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // ---- used by main() and by the tests ----
    CodeView* newTab(const QString& path = QString());     // path empty: an untitled tab
    bool openPath(const QString& path);                    // opens, or switches to the tab that already has it
    void setWorkspaceRoot(const QString& dir);
    CodeView* currentView() const;
    int tabCount() const;
    CodeView* viewAt(int i) const;
    QString tabTitle(int i) const;
    bool closeTab(int index, bool force = false);          // false if the user cancelled
    void restoreSession();

    // actions that tests drive directly
    void nextTab(int step);
    bool renameCurrentFile(const QString& newName, QString* error);
    void showFind(bool replace);
    void findNext(bool backwards = false);
    QLineEdit* findField() const { return find_; }
    QLabel* findCount() const { return findCount_; }
    QString workspaceRoot() const { return root_; }
    FileExplorer* explorer() const { return explorer_; }
    WelcomePage* welcome() const { return welcome_; }
    bool welcomeShown() const;                             // true when no file is open
    void addRecentFolder(const QString& dir);
    QStringList recentFiles() const;
    QStringList recentFolders() const;

    // terminal panel, settings window, shortcuts window
    TerminalPanel* terminal() const { return terminal_; }
    void showTerminal(bool show);
    bool terminalVisible() const;
    SettingsDialog* showSettings(const QString& section = QString());
    ShortcutsDialog* showShortcuts();
    bool runCurrent();                                     // F5: save, build if needed, run in the terminal
    void stopRun();                                        // Shift+F5

protected:
    void closeEvent(QCloseEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private slots:
    void onTabChanged(int index);
    void onViewStateChanged();
    void saveCurrent();
    void saveCurrentAs();
    void saveAll();
    void openDialog();
    void openFolderDialog();
    void renameDialog();
    void quickOpen();
    void goToLineDialog();
    void onPathRenamed(const QString& oldPath, const QString& newPath);
    void onPathRemoved(const QString& path);
    void updateFind();
    void replaceOne();
    void replaceEverything();
    void closeFind();
    void applyPrefs();                                     // the Settings window changed something

private:
    QTabWidget* tabs_ = nullptr;
    QDockWidget* dock_ = nullptr;
    FileExplorer* explorer_ = nullptr;
    WelcomePage* welcome_ = nullptr;
    QStackedWidget* centralStack_ = nullptr;               // welcome page or the editor (tabs + find bar)
    QWidget* editorArea_ = nullptr;
    QSplitter* split_ = nullptr;                           // editor above, terminal below
    TerminalPanel* terminal_ = nullptr;
    QAction* terminalAct_ = nullptr;
    QAction* runAct_ = nullptr;
    QPointer<SettingsDialog> settings_;
    QPointer<ShortcutsDialog> shortcuts_;
    int terminalHeight_ = 240;
    QLabel* encLabel_ = nullptr;
    QAction* shortcutsAct_ = nullptr;
    QWidget* findBar_ = nullptr;
    QLineEdit* find_ = nullptr;
    QLineEdit* repl_ = nullptr;
    QWidget* replRow_ = nullptr;
    QToolButton* caseBox_ = nullptr;
    QToolButton* wordBox_ = nullptr;
    QLabel* findCount_ = nullptr;
    QLabel* posLabel_ = nullptr;
    QLabel* langLabel_ = nullptr;
    QLabel* eolLabel_ = nullptr;
    QString root_;
    bool rootChosen_ = false;
    int untitled_ = 0;

    void buildActions();
    void buildActivityBar();
    void updateCentral();                                  // welcome page when there are no tabs
    void addRecent(const char* key, const QString& path);
    void buildFindBar();
    void applyDarkTheme();
    void refreshTab(CodeView* v);
    void refreshTitle();
    bool maybeSave(CodeView* v);
    bool saveView(CodeView* v, bool forceDialog);
    int indexOfPath(const QString& path) const;
    void connectView(CodeView* v);
    void runFind(bool backwards, bool fromSelectionStart);
    std::vector<Match> matches_;
    QPushButton* issueBtn_ = nullptr;
    SearchOptions searchOptions() const;
    void highlightAll();
};
