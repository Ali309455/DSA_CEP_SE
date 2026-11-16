// FileExplorer.h - DevPad's file manager, shown in the left sidebar (phase 5).
//
// A folder tree with a toolbar (new file, new folder, refresh, collapse all), a filter box that
// finds files anywhere below the folder (the engine's fuzzy Quick Open ranking), a context menu
// (new, rename, delete, cut / copy / paste, duplicate, copy path, reveal in the system file
// manager), inline rename (F2), Delete / Shift+Delete, and drag and drop to move or copy.
//
// All file operations go through the engine (FileIO.h); deleting uses the Recycle Bin / Trash.
// The window keeps open tabs in step through the pathRenamed / pathRemoved signals.
#pragma once
#include <QStringList>
#include <QWidget>
#include <functional>

class QFileSystemModel;
class QLabel;
class QLineEdit;
class QListWidget;
class QModelIndex;
class QStackedWidget;
class QTreeView;
class ExplorerModel;

class FileExplorer : public QWidget {
    Q_OBJECT
public:
    explicit FileExplorer(QWidget* parent = nullptr);

    void setRoot(const QString& dir);
    QString root() const { return root_; }

    // Expand the tree down to 'path', select it and scroll to it (nothing if it is not below the root).
    void reveal(const QString& path);
    QString currentPath() const;
    QStringList selectedPaths() const;           // without items that sit inside another selected folder
    QString targetFolder() const;                // where "New File" goes: the selected folder, the selected file's folder, or the root

    // ---- operations without dialogs (the menus, drag and drop and the tests use them) ----
    // On failure: an empty string / false and the reason in *error.
    QString createFile(const QString& folder, const QString& name, QString* error);
    QString createFolder(const QString& folder, const QString& name, QString* error);
    QString renamePath(const QString& path, const QString& newName, QString* error);
    QString movePath(const QString& path, const QString& destFolder, QString* error);
    QString copyPath(const QString& path, const QString& destFolder, QString* error);
    bool removePath(const QString& path, bool permanently, QString* error);   // !permanently: Recycle Bin / Trash

    // several at once; returns the new paths, failures are added to *errors
    QStringList transfer(const QStringList& paths, const QString& destFolder, bool move, QStringList* errors);
    QStringList removePaths(const QStringList& paths, bool permanently, QStringList* errors);

    // the explorer's own clipboard (Ctrl+X / Ctrl+C / Ctrl+V in the tree)
    void setClipboard(const QStringList& paths, bool cut);
    bool canPaste() const { return !clip_.isEmpty(); }
    QStringList paste(const QString& destFolder, QStringList* errors);

    // filter box: shows the best matching files from the whole folder instead of the tree
    void setFilterText(const QString& text);
    QStringList filterMatches() const;           // relative paths, best first

    QTreeView* tree() const { return tree_; }
    QLineEdit* filterField() const { return filter_; }
    QFileSystemModel* model() const;

    // Asked before deleting: how many open tabs at or below 'path' have unsaved changes.
    std::function<int(const QString&)> unsavedCount;

signals:
    void openRequested(const QString& path);
    void pathRenamed(const QString& oldPath, const QString& newPath);   // rename or move, file or folder
    void pathRemoved(const QString& path);
    void openFolderRequested();

public slots:
    void newFileDialog();
    void newFolderDialog();
    void renameSelected();                       // inline editor in the tree
    void deleteSelected(bool permanently = false);
    void duplicateSelected();
    void refresh();                              // rebuilds the tree, keeps expanded folders
    void collapseAll();

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    friend class ExplorerModel;
    QString root_;
    ExplorerModel* model_ = nullptr;
    QWidget* header_ = nullptr;
    QLabel* title_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QStackedWidget* pages_ = nullptr;            // 0: no folder, 1: tree, 2: filter results
    QTreeView* tree_ = nullptr;
    QListWidget* results_ = nullptr;
    QStringList files_;                          // cached file list for the filter (relative paths)
    QStringList clip_;
    bool clipCut_ = false;
    QString mapFrom_, mapTo_;
    QString revealPending_;                      // reveal() retries while folders are still loading
    QString lastRevealed_;
    int revealTries_ = 0;
    QStringList pendingExpand_;                  // folders to re-open after a rebuild, once loaded                    // a rename / move that happened while the model was rebuilt

    ExplorerModel* makeModel();
    void rebuildModel(const std::function<void()>& op);
    void showMenu(const QPoint& pos);
    void onClicked(const QModelIndex& idx);
    void openResult();
    void revealInSystem(const QString& path);
    void revealNow();
    bool expandDown(const QString& path, bool includeLast);
    void expandPending();
    void warn(const QString& title, const QStringList& errors);
    void filesChanged() { files_.clear(); }
    QStringList expandedPaths() const;
};
