// MainWindow.h - DevPad's main window (phase 5, Qt Widgets).
//
// Tabs (one CodeView each), menus + keyboard shortcuts, find / replace bar, Quick Open
// (Ctrl+P), file manager sidebar (FileExplorer), rename file (F2), session restore. All text editing and all
// searching is done by the engine; this class wires it to the user interface.
#pragma once
#include <QList>
#include <QMainWindow>
#include <QPointer>
#include <QString>
#include <vector>
#include "Diagnostics.h"
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
class ProblemsPanel;
class SyntaxChecker;
class QTimer;
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
    void toggleTheme();                                    // Midnight <-> Daylight, Nord <-> Solarized Light (Ctrl+Shift+L)
    void setTheme(const QString& id);                      // one of Theme::schemeIds()
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

    // terminal panel, problems panel (one tab strip under the editor), settings window, shortcuts window
    TerminalPanel* terminal() const { return terminal_; }
    void showTerminal(bool show);
    bool terminalVisible() const;
    ProblemsPanel* problems() const { return problems_; }
    void showProblems(bool show);
    bool problemsVisible() const;
    void checkNow();                                       // run the compiler's syntax check on the current file
    void editSnippets();                                   // the Snippets window
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
    void onThemeChanged();                                 // the theme or the accent colour changed
    void applyPrefs();                                     // the Settings window changed something

private:
    QTabWidget* tabs_ = nullptr;
    QDockWidget* dock_ = nullptr;
    FileExplorer* explorer_ = nullptr;
    WelcomePage* welcome_ = nullptr;
    QStackedWidget* centralStack_ = nullptr;               // welcome page or the editor (tabs + find bar)
    QWidget* editorArea_ = nullptr;
    QSplitter* split_ = nullptr;                           // editor above, Problems | Terminal below
    QTabWidget* bottom_ = nullptr;
    TerminalPanel* terminal_ = nullptr;
    ProblemsPanel* problems_ = nullptr;
    SyntaxChecker* checker_ = nullptr;
    QTimer* checkTimer_ = nullptr;
    QAction* problemsAct_ = nullptr;
    QPointer<CodeView> checkView_;                         // the tab the running check belongs to
    unsigned checkRevision_ = 0;                           // its text version when the check started
    quint64 checkToken_ = 0;
    bool liveCheck_ = true;                                // Run > Check Code While Typing
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

    bool bottomVisible() const;
    void showBottom(QWidget* page);
    void hideBottom();
    void syncBottomActions();
    void scheduleCheck(int delayMs = 900);
    void refreshProblems();
    void jumpToProblem(const QString& file, int line, int col);
    void onCheckFinished(const QString& path, quint64 token, const std::vector<Diagnostic>& diags, const QString& message);

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
    QPushButton* themeBtn_ = nullptr;                      // status bar: shows the theme, click toggles light / dark
    QList<QAction*> themeActs_;                            // View > Theme, one per theme (checkable)
    SearchOptions searchOptions() const;
    void highlightAll();
};
