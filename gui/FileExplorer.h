// FileExplorer.h - folder tree in the left sidebar (like VS Code's Explorer).
// Double click a file to open it; right click for new file / folder, rename, delete.
// The file operations are plain methods (no dialogs inside) so they can be tested.
#pragma once
#include <QWidget>

class QFileSystemModel;
class QLabel;
class QLineEdit;
class QModelIndex;
class QStackedWidget;
class QToolButton;
class QTreeView;

class FileExplorer : public QWidget {
    Q_OBJECT
public:
    explicit FileExplorer(QWidget* parent = nullptr);

    QString rootPath() const { return root_; }
    bool setRootPath(const QString& dir, QString* error = nullptr);   // false + message if unusable
    void clearRoot();
    void setCurrentFile(const QString& path);                         // highlight (no signal)
    QTreeView* tree() const { return tree_; }

    // ---- file operations: return false and fill 'error' on failure, never throw ----
    static bool validName(const QString& name, QString* why = nullptr);
    bool createFile(const QString& dir, const QString& name, QString* error, QString* createdPath = nullptr);
    bool createFolder(const QString& dir, const QString& name, QString* error, QString* createdPath = nullptr);
    bool renamePath(const QString& path, const QString& newName, QString* error, QString* newPath = nullptr);
    bool removePath(const QString& path, bool toTrash, QString* error);
    void activate(const QString& path);       // file: emit fileActivated, folder: expand / collapse

signals:
    void fileActivated(const QString& path);
    void fileRenamed(const QString& oldPath, const QString& newPath);   // files and folders
    void pathRemoved(const QString& path);
    void rootChanged(const QString& path);    // empty = folder closed
    void openFolderRequested();
    void message(const QString& text);

private:
    QFileSystemModel* model_;
    QTreeView* tree_;
    QStackedWidget* stack_;
    QLabel* title_;
    QLineEdit* searchBox_;
    QToolButton* refreshBtn_;
    QString root_;
    QString filterText_;
    bool showHidden_ = false;

    QString pathAt(const QModelIndex& i) const;
    QString targetDir(const QModelIndex& i) const;
    void showMenu(const QPoint& p);
    void uiNewFile(const QString& dir);
    void uiNewFolder(const QString& dir);
    void uiRename(const QString& path);
    void uiDelete(const QString& path);
    void applyFilter();

private slots:
    void filterFiles(const QString& text);
};
