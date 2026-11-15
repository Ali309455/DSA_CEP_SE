// MainWindow.h - DevPad's main window (phase 5, Qt Widgets).
//
// Tabs (one CodeView each), menus + keyboard shortcuts, find / replace bar, Quick Open
// (Ctrl+P), file tree sidebar, rename file (F2), session restore. All text editing and all
// searching is done by the engine; this class wires it to the user interface.
#pragma once
#include <QMainWindow>
#include <QString>
#include <vector>
#include "Search.h"

class CodeView;
class QTabWidget;
class QLineEdit;
class QCheckBox;
class QLabel;
class QDockWidget;
class QFileSystemModel;
class QTreeView;
class QModelIndex;
class QPushButton;

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
    void onTreeActivated(const QModelIndex& idx);
    void updateFind();
    void replaceOne();
    void replaceEverything();
    void closeFind();

private:
    QTabWidget* tabs_ = nullptr;
    QDockWidget* dock_ = nullptr;
    QTreeView* tree_ = nullptr;
    QFileSystemModel* fsModel_ = nullptr;
    QWidget* findBar_ = nullptr;
    QLineEdit* find_ = nullptr;
    QLineEdit* repl_ = nullptr;
    QWidget* replRow_ = nullptr;
    QCheckBox* caseBox_ = nullptr;
    QCheckBox* wordBox_ = nullptr;
    QLabel* findCount_ = nullptr;
    QLabel* posLabel_ = nullptr;
    QLabel* langLabel_ = nullptr;
    QLabel* eolLabel_ = nullptr;
    QString root_;
    bool rootChosen_ = false;
    int untitled_ = 0;

    void buildActions();
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
