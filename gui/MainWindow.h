// MainWindow.h - menus, toolbar, panels and status bar around the CodeEditor.
#pragma once
#include <QMainWindow>
#include <QSplitter>
#include <QTimer>
#include "Theme.h"

struct EditorSettings;

class CodeEditor;
class FindBar;
class FileExplorer;
class QDockWidget;
class RunManager;
class QAction;
class QLabel;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QSplitter;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    CodeEditor* editor() const { return editor_; }
    FindBar* findBar() const { return find_; }
    RunManager* runner() const { return run_; }
    bool openPath(const QString& path, bool silent = false);
    bool openFolder(const QString& dir, bool silent = false);   // show it in the Explorer sidebar
    FileExplorer* explorer() const { return explorer_; }   // false on failure (message shown unless silent)
    bool maybeSave();                                          // false = user cancelled
    void applyTheme(const QString& name);
    QString snippetFile() const;

protected:
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private slots:
    void fileNew();
    void fileOpen();
    bool fileSave();
    bool fileSaveAs();
    void refreshState();
    void refreshProblems();
    void refreshInspector();
    void runClicked();
    void goToLineDialog();
    void chooseFolder();
    void nextProblem();
    void themeNord();
    void themeSolarizedLight();
    void openSettingsDialog();
    void matchBracket();
    void onAutoSaveTimer();
    void applySettings(const EditorSettings& s);

private:
    CodeEditor* editor_;
    FindBar* find_;
    RunManager* run_;
    FileExplorer* explorer_;
    QDockWidget* explorerDock_;
    QTabWidget* tabs_;
    QTreeWidget* problems_;
    QPlainTextEdit* output_;
    QLineEdit* input_;
    QPlainTextEdit* inspector_;
    QLabel *posLabel_, *bracketLabel_, *historyLabel_, *infoLabel_;
    QLabel* runStatusLabel_;
    QAction *undoA_, *redoA_, *runA_, *stopA_, *saveA_, *wrapA_, *autoCloseA_;
    QAction *darkA_, *lightA_, *nordA_, *solarA_;
    QMenu* recentMenu_;
    QString themeName_ = "Midnight";
    QString activeFile_;
    QString compilerFlags_;
    QStringList recent_;
    QList<QTreeWidgetItem*> compilerItems_;
    QTimer autoSaveTimer_;
    bool autoSaveEnabled_ = false;
    int autoSaveIntervalSec_ = 3;
    bool showUnsavedIndicator_ = true;

    void buildMenus();
    void loadSettings();
    void saveSettings();
    void addRecent(const QString& path);
    void rebuildRecentMenu();
    void jumpTo(int line1, int col1);
    void addCompilerDiagnostic(int line, int col, const QString& sev, const QString& text);
    void appendOutput(const QString& text, bool isError);
};
