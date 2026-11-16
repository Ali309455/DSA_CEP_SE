#include "FileExplorer.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <string>

#include "FileIO.h"
#include "Icons.h"
#include "Workspace.h"

// On Windows the folder watcher of QFileSystemModel keeps the folders it shows open, and an open
// folder cannot be renamed, moved or deleted (QTBUG-65683). Folder operations therefore drop the
// model (which closes every watch), do the work, and build a fresh one.
#ifdef Q_OS_WIN
static const bool kReleaseWatchers = true;
static const char* kTrashName = "Recycle Bin";
#else
static const bool kReleaseWatchers = false;
static const char* kTrashName = "Trash";
#endif

static std::string u8(const QString& s) { return s.toUtf8().toStdString(); }
static QString qs(const std::string& s) { return QString::fromUtf8(s.c_str()); }

// ---------------------------------------------------------------- model and delegate

// QFileSystemModel whose inline rename and drag and drop go through FileExplorer (and so through
// the engine). Both are done a moment later, outside the model's own call, because a folder
// operation may replace the model.
class ExplorerModel : public QFileSystemModel {
public:
    explicit ExplorerModel(FileExplorer* ex) : QFileSystemModel(ex), ex_(ex) {}

    bool setData(const QModelIndex& idx, const QVariant& value, int role) override {
        if (!idx.isValid() || idx.column() != 0 || role != Qt::EditRole) return false;
        const QString path = filePath(idx);
        const QString name = value.toString().trimmed();
        if (name.isEmpty() || name == fileName(idx)) return false;
        QTimer::singleShot(0, ex_, [ex = ex_, path, name]() {
            QString err;
            const QString np = ex->renamePath(path, name, &err);
            if (np.isEmpty()) ex->warn("Rename", {err});
            else ex->reveal(np);
        });
        return true;
    }

    Qt::DropActions supportedDropActions() const override { return Qt::CopyAction | Qt::MoveAction; }

    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int, int, const QModelIndex& parent) override {
        if (!data || !data->hasUrls() || (action != Qt::MoveAction && action != Qt::CopyAction)) return false;
        QString dest = parent.isValid() ? filePath(parent) : ex_->root();
        if (!QFileInfo(dest).isDir()) dest = QFileInfo(dest).absolutePath();
        QStringList paths;
        for (const QUrl& u : data->urls())
            if (u.isLocalFile()) paths << u.toLocalFile();
        if (paths.isEmpty() || dest.isEmpty()) return false;
        const bool move = action == Qt::MoveAction;
        QTimer::singleShot(0, ex_, [ex = ex_, paths, dest, move]() {
            QStringList errors;
            const QStringList done = ex->transfer(paths, dest, move, &errors);
            if (!done.isEmpty()) ex->reveal(done.last());
            ex->warn(move ? "Move" : "Copy", errors);
        });
        return true;
    }

private:
    FileExplorer* ex_;
};

namespace {
// Inline rename: select the name without its extension, like other editors do.
class RenameDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    // DevPad's own icons: a badge per file type, folders that open when expanded
    void initStyleOption(QStyleOptionViewItem* opt, const QModelIndex& idx) const override {
        QStyledItemDelegate::initStyleOption(opt, idx);
        const QFileSystemModel* m = qobject_cast<const QFileSystemModel*>(idx.model());
        if (!m || idx.column() != 0) return;
        const QTreeView* tree = qobject_cast<const QTreeView*>(parent());
        opt->icon = m->isDir(idx) ? Icons::folder(tree && tree->isExpanded(idx)) : Icons::forFile(m->fileName(idx));
        opt->features |= QStyleOptionViewItem::HasDecoration;
        opt->decorationSize = QSize(16, 16);
    }
    void setEditorData(QWidget* editor, const QModelIndex& idx) const override {
        QStyledItemDelegate::setEditorData(editor, idx);
        QLineEdit* le = qobject_cast<QLineEdit*>(editor);
        if (!le) return;
        const QFileSystemModel* m = qobject_cast<const QFileSystemModel*>(idx.model());
        const int dot = le->text().lastIndexOf('.');
        const bool file = m && !m->isDir(idx);
        QTimer::singleShot(0, le, [le, dot, file]() {
            if (file && dot > 0) le->setSelection(0, dot);
            else le->selectAll();
        });
    }
};

QToolButton* toolButton(QWidget* parent, Icons::Glyph g, const QString& tip) {
    QToolButton* b = new QToolButton(parent);
    b->setIcon(Icons::get(g, QColor(0xa8, 0xa8, 0xa8), QColor(0xf0, 0xf0, 0xf0)));
    b->setIconSize(QSize(16, 16));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

// The model asks this for icons in a background thread; DevPad draws its own icons in the
// delegate instead (GUI thread), so this only has to return nothing, quickly.
class NoIcons : public QFileIconProvider {
public:
    QIcon icon(IconType) const override { return QIcon(); }
    QIcon icon(const QFileInfo&) const override { return QIcon(); }
};
NoIcons* noIcons() { static NoIcons p; return &p; }

// Tree with chevrons instead of the platform's branch arrows.
class ExplorerTree : public QTreeView {
public:
    using QTreeView::QTreeView;
protected:
    void drawBranches(QPainter* p, const QRect& rect, const QModelIndex& idx) const override {
        if (!model() || !model()->hasChildren(idx)) return;
        const bool open = isExpanded(idx);
        const QIcon icon = Icons::get(open ? Icons::ChevronDown : Icons::ChevronRight, QColor(0xa0, 0xa0, 0xa0), QColor(0xe0, 0xe0, 0xe0));
        const int w = indentation();
        const QRect box(rect.right() - w + 1, rect.top(), w, rect.height());
        const QSize sz(14, 14);
        icon.paint(p, QRect(box.center() - QPoint(sz.width() / 2 - 1, sz.height() / 2 - 1), sz));
    }
};
}  // namespace

// ---------------------------------------------------------------- construction

FileExplorer::FileExplorer(QWidget* parent) : QWidget(parent) {
    setObjectName("fileExplorer");
    QVBoxLayout* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // header: folder name + toolbar
    header_ = new QWidget(this);
    header_->setObjectName("explorerHeader");
    QHBoxLayout* hl = new QHBoxLayout(header_);
    hl->setContentsMargins(14, 6, 6, 4);
    hl->setSpacing(1);
    title_ = new QLabel("EXPLORER", header_);
    title_->setObjectName("explorerTitle");
    hl->addWidget(title_, 1);
    QToolButton* bNewFile = toolButton(header_, Icons::NewFile, "New File…");
    QToolButton* bNewFolder = toolButton(header_, Icons::NewFolder, "New Folder…");
    QToolButton* bRefresh = toolButton(header_, Icons::Refresh, "Refresh Explorer");
    QToolButton* bCollapse = toolButton(header_, Icons::Collapse, "Collapse Folders");
    for (QToolButton* b : {bNewFile, bNewFolder, bRefresh, bCollapse}) { b->setObjectName("explorerButton"); hl->addWidget(b); }
    connect(bNewFile, &QToolButton::clicked, this, &FileExplorer::newFileDialog);
    connect(bNewFolder, &QToolButton::clicked, this, &FileExplorer::newFolderDialog);
    connect(bRefresh, &QToolButton::clicked, this, &FileExplorer::refresh);
    connect(bCollapse, &QToolButton::clicked, this, &FileExplorer::collapseAll);
    lay->addWidget(header_);

    filter_ = new QLineEdit(this);
    filter_->setObjectName("explorerFilter");
    filter_->setPlaceholderText("Filter files");
    filter_->setToolTip("Finds files anywhere in the folder by a few letters of the name, e.g. \"mw\" finds MainWindow.cpp");
    filter_->addAction(Icons::get(Icons::Search, QColor(0x80, 0x80, 0x80)), QLineEdit::LeadingPosition);
    filter_->setClearButtonEnabled(true);
    filter_->installEventFilter(this);
    QWidget* filterBox = new QWidget(this);
    QHBoxLayout* fl = new QHBoxLayout(filterBox);
    fl->setContentsMargins(8, 2, 8, 6);
    fl->addWidget(filter_);
    lay->addWidget(filterBox);
    connect(filter_, &QLineEdit::textChanged, this, &FileExplorer::setFilterText);
    connect(filter_, &QLineEdit::returnPressed, this, &FileExplorer::openResult);

    pages_ = new QStackedWidget(this);
    lay->addWidget(pages_, 1);

    // page 0: no folder yet
    QWidget* empty = new QWidget(pages_);
    QVBoxLayout* el = new QVBoxLayout(empty);
    el->setContentsMargins(16, 16, 16, 16);
    QLabel* msg = new QLabel("You have not opened a folder yet.", empty);
    msg->setWordWrap(true);
    msg->setObjectName("explorerHint");
    QPushButton* openBtn = new QPushButton("Open Folder", empty);
    openBtn->setObjectName("explorerOpen");
    connect(openBtn, &QPushButton::clicked, this, &FileExplorer::openFolderRequested);
    el->addWidget(msg);
    el->addSpacing(8);
    el->addWidget(openBtn);
    el->addStretch(1);
    pages_->addWidget(empty);

    // page 1: the tree
    tree_ = new ExplorerTree(pages_);
    tree_->setObjectName("explorerTree");
    tree_->setHeaderHidden(true);
    tree_->setUniformRowHeights(true);
    tree_->setAnimated(true);
    tree_->setIndentation(16);
    tree_->setIconSize(QSize(16, 16));
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);    // F2 and the menu start the editor themselves
    tree_->setDragEnabled(true);
    tree_->setAcceptDrops(true);
    tree_->setDropIndicatorShown(true);
    tree_->setDragDropMode(QAbstractItemView::DragDrop);
    tree_->setDefaultDropAction(Qt::MoveAction);                  // drag = move, Ctrl+drag = copy
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->setItemDelegate(new RenameDelegate(tree_));
    tree_->installEventFilter(this);
    connect(tree_, &QTreeView::clicked, this, &FileExplorer::onClicked);
    connect(tree_, &QTreeView::activated, this, &FileExplorer::onClicked);
    connect(tree_, &QWidget::customContextMenuRequested, this, &FileExplorer::showMenu);
    pages_->addWidget(tree_);

    // page 2: filter results
    results_ = new QListWidget(pages_);
    results_->setObjectName("explorerResults");
    results_->setIconSize(QSize(16, 16));
    results_->installEventFilter(this);
    connect(results_, &QListWidget::itemActivated, this, &FileExplorer::openResult);
    connect(results_, &QListWidget::itemClicked, this, &FileExplorer::openResult);
    pages_->addWidget(results_);

    model_ = makeModel();
    tree_->setModel(model_);
    setRoot(QString());
}

ExplorerModel* FileExplorer::makeModel() {
    ExplorerModel* m = new ExplorerModel(this);
    m->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m->setReadOnly(false);                       // allows inline rename and dropping onto folders
    m->setIconProvider(noIcons());
    connect(m, &QFileSystemModel::directoryLoaded, this, [this]() {
        if (!pendingExpand_.isEmpty()) expandPending();
        // a folder that finishes loading can reshuffle its rows and drop the selection: put the
        // revealed file back, unless the user has selected something else meanwhile
        if (!lastRevealed_.isEmpty() && revealPending_.isEmpty() && model_ && tree_->selectionModel() &&
            !tree_->selectionModel()->hasSelection() && QFileInfo::exists(lastRevealed_)) {
            const QString p = lastRevealed_;
            QTimer::singleShot(0, this, [this, p]() { if (!tree_->selectionModel()->hasSelection()) reveal(p); });
        }
    });
    return m;
}

QFileSystemModel* FileExplorer::model() const { return model_; }

void FileExplorer::setRoot(const QString& dir) {
    const QFileInfo fi(dir);
    root_ = (!dir.isEmpty() && fi.isDir()) ? fi.canonicalFilePath() : QString();
    files_.clear();
    filter_->clear();
    if (root_.isEmpty()) {
        title_->setText("EXPLORER");
        title_->setToolTip(QString());
        filter_->parentWidget()->hide();
        pages_->setCurrentIndex(0);
        return;
    }
    model_->setRootPath(root_);
    tree_->setRootIndex(model_->index(root_));
    for (int c = 1; c < model_->columnCount(); ++c) tree_->hideColumn(c);
    title_->setText(fi.fileName().isEmpty() ? root_ : fi.fileName().toUpper());
    title_->setToolTip(QDir::toNativeSeparators(root_));
    filter_->parentWidget()->show();
    pages_->setCurrentIndex(1);
}

// ---------------------------------------------------------------- tree helpers

QString FileExplorer::currentPath() const {
    const QModelIndex idx = tree_->currentIndex();
    return idx.isValid() ? model_->filePath(idx) : QString();
}

QStringList FileExplorer::selectedPaths() const {
    QStringList all;
    for (const QModelIndex& idx : tree_->selectionModel()->selectedRows(0)) all << model_->filePath(idx);
    if (all.isEmpty() && tree_->currentIndex().isValid()) all << currentPath();
    QStringList out;
    for (const QString& p : all) {
        bool inside = false;
        for (const QString& q : all)
            if (q != p && isSameOrInside(u8(p), u8(q))) { inside = true; break; }
        if (!inside) out << p;
    }
    return out;
}

QString FileExplorer::targetFolder() const {
    const QString p = currentPath();
    if (p.isEmpty()) return root_;
    const QFileInfo fi(p);
    return fi.isDir() ? p : fi.absolutePath();
}

void FileExplorer::reveal(const QString& path) {
    if (root_.isEmpty() || path.isEmpty() || !isSameOrInside(u8(path), u8(root_))) return;
    revealPending_ = path;
    lastRevealed_ = path;
    revealTries_ = 0;
    revealNow();
}

// The tree only knows a folder's contents once that folder has been opened (they load in the
// background), so walk down from the root, opening each folder on the way, as far as is loaded.
// True if every folder on the way was there.
bool FileExplorer::expandDown(const QString& path, bool includeLast) {
    if (root_.isEmpty() || !model_) return false;
    QStringList parts = QDir(root_).relativeFilePath(path).split('/', Qt::SkipEmptyParts);
    parts.removeAll(".");
    if (!parts.isEmpty() && parts.first() == "..") return false;
    if (!includeLast && !parts.isEmpty()) parts.removeLast();
    QString cur = root_;
    for (const QString& part : parts) {
        cur += '/' + part;
        const QModelIndex idx = model_->index(cur);
        if (!idx.isValid()) return false;
        tree_->expand(idx);
    }
    return true;
}

void FileExplorer::revealNow() {
    if (revealPending_.isEmpty() || !model_) return;
    bool done = false;
    if (expandDown(revealPending_, false)) {
        const QModelIndex idx = model_->index(revealPending_);
        if (idx.isValid()) {
            tree_->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            tree_->scrollTo(idx);
            done = true;
        }
    }
    if (done || ++revealTries_ > 40) { revealPending_.clear(); return; }
    QTimer::singleShot(50, this, [this]() { revealNow(); });   // a folder on the way is still loading
}

// After a rebuild: re-open the folders that were open, each as soon as its parent has loaded.
void FileExplorer::expandPending() {
    for (int i = pendingExpand_.size() - 1; i >= 0; --i)
        if (expandDown(pendingExpand_[i], true)) pendingExpand_.removeAt(i);
}

QStringList FileExplorer::expandedPaths() const {
    QStringList out;
    std::function<void(const QModelIndex&)> walk = [&](const QModelIndex& parent) {
        for (int r = 0; r < model_->rowCount(parent); ++r) {
            const QModelIndex c = model_->index(r, 0, parent);
            if (tree_->isExpanded(c)) { out << model_->filePath(c); walk(c); }
        }
    };
    if (!root_.isEmpty()) walk(tree_->rootIndex());
    return out;
}

void FileExplorer::rebuildModel(const std::function<void()>& op) {
    const QStringList open = expandedPaths();
    const QString cur = currentPath();
    QItemSelectionModel* oldSel = tree_->selectionModel();
    tree_->setModel(nullptr);
    delete oldSel;
    delete model_;                               // stops the watcher: no folder is held open any more
    model_ = nullptr;
    mapFrom_.clear();
    mapTo_.clear();
    if (op) op();
    model_ = makeModel();
    tree_->setModel(model_);
    const QString keepFilter = filter_->text();
    const QSignalBlocker block(filter_);
    setRoot(root_);
    filter_->setText(keepFilter);
    if (!keepFilter.isEmpty()) setFilterText(keepFilter);
    auto mapped = [this](const QString& p) {
        if (mapFrom_.isEmpty() || !(p == mapFrom_ || p.startsWith(mapFrom_ + '/'))) return p;
        return mapTo_ + p.mid(mapFrom_.size());
    };
    pendingExpand_.clear();
    for (const QString& p : open) pendingExpand_ << mapped(p);
    expandPending();
    if (!cur.isEmpty()) reveal(mapped(cur));
    mapFrom_.clear();
    mapTo_.clear();
}

void FileExplorer::refresh() {
    if (root_.isEmpty()) return;
    files_.clear();
    rebuildModel(nullptr);
}

void FileExplorer::collapseAll() { tree_->collapseAll(); }

void FileExplorer::onClicked(const QModelIndex& idx) {
    if (!idx.isValid() || QApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) return;
    const QString p = model_->filePath(idx);
    if (QFileInfo(p).isFile()) emit openRequested(p);
}

// ---------------------------------------------------------------- operations

QString FileExplorer::createFile(const QString& folder, const QString& name, QString* error) {
    std::string np;
    const IOResult r = ::createFile(u8(folder), u8(name.trimmed()), &np);
    if (!r.ok) { if (error) *error = qs(r.message); return QString(); }
    filesChanged();
    return qs(np);
}

QString FileExplorer::createFolder(const QString& folder, const QString& name, QString* error) {
    std::string np;
    const IOResult r = ::createFolder(u8(folder), u8(name.trimmed()), &np);
    if (!r.ok) { if (error) *error = qs(r.message); return QString(); }
    filesChanged();
    return qs(np);
}

QString FileExplorer::renamePath(const QString& path, const QString& newName, QString* error) {
    std::string np;
    IOResult r{false, ""};
    auto op = [&]() {
        r = ::renamePath(u8(path), u8(newName.trimmed()), &np);
        if (r.ok) { mapFrom_ = path; mapTo_ = qs(np); }
    };
    if (kReleaseWatchers && QFileInfo(path).isDir()) rebuildModel(op);
    else op();
    if (!r.ok) { if (error) *error = qs(r.message); return QString(); }
    filesChanged();
    if (qs(np) != path) emit pathRenamed(path, qs(np));
    return qs(np);
}

QString FileExplorer::movePath(const QString& path, const QString& destFolder, QString* error) {
    QStringList errors;
    const QStringList done = transfer({path}, destFolder, true, &errors);
    if (done.isEmpty() && error) *error = errors.value(0);
    return done.value(0);
}

QString FileExplorer::copyPath(const QString& path, const QString& destFolder, QString* error) {
    QStringList errors;
    const QStringList done = transfer({path}, destFolder, false, &errors);
    if (done.isEmpty() && error) *error = errors.value(0);
    return done.value(0);
}

QStringList FileExplorer::transfer(const QStringList& paths, const QString& destFolder, bool move, QStringList* errors) {
    QStringList done;
    QList<QPair<QString, QString>> moved;
    auto op = [&]() {
        for (const QString& p : paths) {
            std::string np;
            const IOResult r = move ? ::movePath(u8(p), u8(destFolder), &np) : ::copyPath(u8(p), u8(destFolder), &np);
            if (!r.ok) { if (errors) *errors << qs(r.message); continue; }
            done << qs(np);
            if (move && qs(np) != p) {
                moved.append({p, qs(np)});
                if (paths.size() == 1) { mapFrom_ = p; mapTo_ = qs(np); }
            }
        }
    };
    bool anyDir = false;
    for (const QString& p : paths) anyDir = anyDir || QFileInfo(p).isDir();
    if (kReleaseWatchers && move && anyDir) rebuildModel(op);
    else op();
    if (!done.isEmpty()) filesChanged();
    for (const auto& m : moved) emit pathRenamed(m.first, m.second);
    return done;
}

static bool removeOne(const QString& path, bool permanently, QString* error) {
    const QFileInfo fi(path);
    if (!fi.exists() && !fi.isSymLink()) { *error = "'" + fi.fileName() + "' does not exist any more."; return false; }
    if (!permanently) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        if (QFile::moveToTrash(path)) return true;
#endif
        *error = QString("Cannot move '%1' to the %2.").arg(fi.fileName(), kTrashName);
        return false;
    }
    const bool ok = (fi.isDir() && !fi.isSymLink()) ? QDir(path).removeRecursively() : QFile::remove(path);
    if (!ok) *error = "Cannot delete '" + fi.fileName() + "' (is it open in another program?).";
    return ok;
}

QStringList FileExplorer::removePaths(const QStringList& paths, bool permanently, QStringList* errors) {
    QStringList done;
    auto op = [&]() {
        for (const QString& p : paths) {
            QString err;
            if (removeOne(p, permanently, &err)) done << p;
            else if (errors) *errors << err;
        }
    };
    bool anyDir = false;
    for (const QString& p : paths) anyDir = anyDir || QFileInfo(p).isDir();
    if (kReleaseWatchers && anyDir) rebuildModel(op);
    else op();
    if (!done.isEmpty()) filesChanged();
    for (const QString& p : done) emit pathRemoved(p);
    return done;
}

bool FileExplorer::removePath(const QString& path, bool permanently, QString* error) {
    QStringList errors;
    const bool ok = !removePaths({path}, permanently, &errors).isEmpty();
    if (!ok && error) *error = errors.value(0);
    return ok;
}

void FileExplorer::setClipboard(const QStringList& paths, bool cut) {
    clip_ = paths;
    clipCut_ = cut;
    // also put them on the system clipboard as text, handy for pasting a path somewhere
    QStringList native;
    for (const QString& p : paths) native << QDir::toNativeSeparators(p);
    QApplication::clipboard()->setText(native.join('\n'));
}

QStringList FileExplorer::paste(const QString& destFolder, QStringList* errors) {
    if (clip_.isEmpty() || destFolder.isEmpty()) return {};
    const QStringList done = transfer(clip_, destFolder, clipCut_, errors);
    if (clipCut_) clip_.clear();                 // cut + paste is a move: the source is gone
    return done;
}

// ---------------------------------------------------------------- dialogs and menu

void FileExplorer::warn(const QString& title, const QStringList& errors) {
    if (errors.isEmpty()) return;
    QMessageBox::warning(this, title, errors.join('\n'));
}

void FileExplorer::newFileDialog() {
    if (root_.isEmpty()) { emit openFolderRequested(); return; }
    const QString folder = targetFolder();
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New File",
        "File name in " + QDir::toNativeSeparators(QDir(root_).relativeFilePath(folder) == "." ? QFileInfo(root_).fileName() : QDir(root_).relativeFilePath(folder)) + ":",
        QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    QString err;
    const QString p = createFile(folder, name, &err);
    if (p.isEmpty()) { warn("New File", {err}); return; }
    reveal(p);
    emit openRequested(p);
}

void FileExplorer::newFolderDialog() {
    if (root_.isEmpty()) { emit openFolderRequested(); return; }
    const QString folder = targetFolder();
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New Folder", "Folder name:", QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    QString err;
    const QString p = createFolder(folder, name, &err);
    if (p.isEmpty()) { warn("New Folder", {err}); return; }
    reveal(p);
}

void FileExplorer::renameSelected() {
    const QModelIndex idx = tree_->currentIndex();
    if (!idx.isValid()) return;
    tree_->scrollTo(idx);
    tree_->edit(idx);
}

void FileExplorer::deleteSelected(bool permanently) {
    const QStringList paths = selectedPaths();
    if (paths.isEmpty()) return;
    int unsaved = 0;
    if (unsavedCount)
        for (const QString& p : paths) unsaved += unsavedCount(p);
    QString what = paths.size() == 1 ? "'" + QFileInfo(paths[0]).fileName() + "'" : QString("these %1 items").arg(paths.size());
    if (paths.size() == 1 && QFileInfo(paths[0]).isDir()) what += " and everything in it";
    QString text = permanently ? "Delete " + what + " permanently?\nThis cannot be undone."
                               : QString("Move %1 to the %2?").arg(what, kTrashName);
    if (unsaved > 0) text += QString("\n\n%1 open tab(s) with unsaved changes will be closed and those changes lost.").arg(unsaved);
    QMessageBox box(QMessageBox::Warning, permanently ? "Delete Permanently" : "Delete", text, QMessageBox::Cancel, this);
    QPushButton* yes = box.addButton(permanently ? "Delete" : QString("Move to %1").arg(kTrashName), QMessageBox::DestructiveRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != yes) return;

    QStringList errors;
    removePaths(paths, permanently, &errors);
    if (!permanently && !errors.isEmpty()) {
        // no Recycle Bin on that drive (e.g. a USB stick or network share): offer a real delete
        QStringList left;
        for (const QString& p : paths) if (QFileInfo::exists(p)) left << p;
        if (!left.isEmpty() && QMessageBox::question(this, "Delete",
                errors.join('\n') + "\n\nDelete permanently instead? This cannot be undone.") == QMessageBox::Yes) {
            errors.clear();
            removePaths(left, true, &errors);
        } else {
            errors.clear();
        }
    }
    warn("Delete", errors);
}

void FileExplorer::duplicateSelected() {
    QStringList errors;
    QStringList done;
    for (const QString& p : selectedPaths()) done += transfer({p}, QFileInfo(p).absolutePath(), false, &errors);
    if (!done.isEmpty()) reveal(done.last());
    warn("Duplicate", errors);
}

void FileExplorer::revealInSystem(const QString& path) {
#ifdef Q_OS_WIN
    QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(path)});
#elif defined(Q_OS_MACOS)
    QProcess::startDetached("open", {"-R", path});
#else
    const QFileInfo fi(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.isDir() ? path : fi.absolutePath()));
#endif
}

void FileExplorer::showMenu(const QPoint& pos) {
    if (root_.isEmpty()) return;
    const QModelIndex at = tree_->indexAt(pos);
    if (!at.isValid()) tree_->clearSelection();
    else if (!tree_->selectionModel()->isSelected(at)) tree_->setCurrentIndex(at);
    if (!at.isValid()) tree_->setCurrentIndex(QModelIndex());

    const QStringList sel = at.isValid() ? selectedPaths() : QStringList();
    const QString one = sel.size() == 1 ? sel[0] : QString();
    const bool isFile = !one.isEmpty() && QFileInfo(one).isFile();
    const QString dest = targetFolder();

    QMenu m(this);
    if (isFile) {
        m.addAction("Open", this, [this, one]() { emit openRequested(one); });
        m.addSeparator();
    }
    m.addAction("New File…", this, &FileExplorer::newFileDialog);
    m.addAction("New Folder…", this, &FileExplorer::newFolderDialog);
    m.addSeparator();
    if (!sel.isEmpty()) {
        m.addAction("Cut", this, [this, sel]() { setClipboard(sel, true); })->setShortcut(QKeySequence::Cut);
        m.addAction("Copy", this, [this, sel]() { setClipboard(sel, false); })->setShortcut(QKeySequence::Copy);
    }
    QAction* pasteAct = m.addAction("Paste", this, [this, dest]() {
        QStringList errors;
        const QStringList done = paste(dest, &errors);
        if (!done.isEmpty()) reveal(done.last());
        warn("Paste", errors);
    });
    pasteAct->setShortcut(QKeySequence::Paste);
    pasteAct->setEnabled(canPaste());
    if (!sel.isEmpty()) m.addAction("Duplicate", this, &FileExplorer::duplicateSelected);
    m.addSeparator();
    const QStringList forPath = sel.isEmpty() ? QStringList{root_} : sel;
    m.addAction("Copy Path", this, [forPath]() {
        QStringList out;
        for (const QString& p : forPath) out << QDir::toNativeSeparators(p);
        QApplication::clipboard()->setText(out.join('\n'));
    });
    m.addAction("Copy Relative Path", this, [this, forPath]() {
        QStringList out;
        for (const QString& p : forPath) out << QDir::toNativeSeparators(QDir(root_).relativeFilePath(p));
        QApplication::clipboard()->setText(out.join('\n'));
    });
#ifdef Q_OS_WIN
    const char* revealText = "Reveal in File Explorer";
#elif defined(Q_OS_MACOS)
    const char* revealText = "Reveal in Finder";
#else
    const char* revealText = "Open Containing Folder";
#endif
    const QString revealPath = one.isEmpty() ? (sel.isEmpty() ? root_ : sel[0]) : one;
    m.addAction(revealText, this, [this, revealPath]() { revealInSystem(revealPath); });
    if (!sel.isEmpty()) {
        m.addSeparator();
        if (!one.isEmpty()) m.addAction("Rename…", this, &FileExplorer::renameSelected)->setShortcut(QKeySequence(Qt::Key_F2));
        m.addAction("Delete", this, [this]() { deleteSelected(false); })->setShortcut(QKeySequence::Delete);
    } else {
        m.addSeparator();
        m.addAction("Refresh", this, &FileExplorer::refresh);
        m.addAction("Collapse Folders", this, &FileExplorer::collapseAll);
    }
    m.exec(tree_->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------- filter

void FileExplorer::setFilterText(const QString& text) {
    if (filter_->text() != text) { filter_->setText(text); return; }   // textChanged comes back here
    if (root_.isEmpty()) return;
    const QString q = text.trimmed();
    if (q.isEmpty()) { pages_->setCurrentIndex(1); return; }
    if (files_.isEmpty()) {
        const DynArray<std::string> all = listWorkspaceFiles(u8(root_));
        for (size_t i = 0; i < all.size(); ++i) files_ << qs(all[i]);
    }
    DynArray<std::string> list;
    for (const QString& f : files_) list.push_back(u8(f));
    const DynArray<size_t> hits = rankFiles(list, u8(q), 200);
    results_->clear();
    for (size_t i = 0; i < hits.size(); ++i) {
        const QString rel = files_[int(hits[i])];
        QListWidgetItem* it = new QListWidgetItem(Icons::forFile(rel), rel, results_);
        it->setToolTip(QDir::toNativeSeparators(QDir(root_).filePath(rel)));
    }
    if (results_->count() == 0) {
        QListWidgetItem* none = new QListWidgetItem("No matching files", results_);
        none->setFlags(Qt::NoItemFlags);
    } else {
        results_->setCurrentRow(0);
    }
    pages_->setCurrentIndex(2);
}

QStringList FileExplorer::filterMatches() const {
    QStringList out;
    if (pages_->currentIndex() != 2) return out;
    for (int i = 0; i < results_->count(); ++i)
        if (results_->item(i)->flags() & Qt::ItemIsEnabled) out << results_->item(i)->text();
    return out;
}

void FileExplorer::openResult() {
    QListWidgetItem* it = results_->currentItem();
    if (pages_->currentIndex() != 2 || !it || !(it->flags() & Qt::ItemIsEnabled)) return;
    const QString p = QDir(root_).filePath(it->text());
    emit openRequested(p);
    reveal(p);
}

// ---------------------------------------------------------------- keys

bool FileExplorer::eventFilter(QObject* obj, QEvent* e) {
    if (e->type() != QEvent::ShortcutOverride && e->type() != QEvent::KeyPress) return QWidget::eventFilter(obj, e);
    QKeyEvent* k = static_cast<QKeyEvent*>(e);

    if (obj == filter_ || obj == results_) {
        if (e->type() != QEvent::KeyPress) return false;
        if (k->key() == Qt::Key_Escape) { filter_->clear(); tree_->setFocus(); return true; }
        if (obj == filter_ && (k->key() == Qt::Key_Down || k->key() == Qt::Key_Up) && results_->count() > 0) {
            const int row = qBound(0, results_->currentRow() + (k->key() == Qt::Key_Down ? 1 : -1), results_->count() - 1);
            results_->setCurrentRow(row);
            return true;
        }
        if (obj == results_ && (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter)) { openResult(); return true; }
        return false;
    }

    if (obj != tree_) return QWidget::eventFilter(obj, e);   // (while renaming, keys go to the inline editor, not here)
    const bool isCut = k->matches(QKeySequence::Cut), isCopy = k->matches(QKeySequence::Copy), isPaste = k->matches(QKeySequence::Paste);
    const bool del = k->key() == Qt::Key_Delete, f2 = k->key() == Qt::Key_F2;
    const bool enter = (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && k->modifiers() == Qt::NoModifier;
    if (!(isCut || isCopy || isPaste || del || f2 || enter)) return QWidget::eventFilter(obj, e);
    // the window has its own Ctrl+C / F2 / ... shortcuts; while the tree has focus the keys are the tree's
    if (e->type() == QEvent::ShortcutOverride) { e->accept(); return true; }

    if (f2) renameSelected();
    else if (del) deleteSelected(k->modifiers() & Qt::ShiftModifier);
    else if (isCut || isCopy) { const QStringList s = selectedPaths(); if (!s.isEmpty()) setClipboard(s, isCut); }
    else if (isPaste) {
        QStringList errors;
        const QStringList done = paste(targetFolder(), &errors);
        if (!done.isEmpty()) reveal(done.last());
        warn("Paste", errors);
    } else if (enter) {
        const QModelIndex idx = tree_->currentIndex();
        if (!idx.isValid()) return true;
        if (model_->isDir(idx)) tree_->setExpanded(idx, !tree_->isExpanded(idx));
        else emit openRequested(model_->filePath(idx));
    }
    return true;
}
