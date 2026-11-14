#include "FileExplorer.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QGuiApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

FileExplorer::FileExplorer(QWidget* parent) : QWidget(parent) {
    model_ = new QFileSystemModel(this);
    model_->setReadOnly(true);                 // all changes go through our methods
    applyFilter();

    tree_ = new QTreeView(this);
    tree_->setAnimated(true);
    tree_->setItemsExpandable(true);
    tree_->setWordWrap(false);
    tree_->setObjectName("explorerTree");
    tree_->setModel(model_);
    tree_->setHeaderHidden(true);
    tree_->setUniformRowHeights(true);
    tree_->setIndentation(16);
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setExpandsOnDoubleClick(false);     // we decide: files open, folders toggle
    tree_->setStyleSheet("QTreeView::item { height: 22px; padding-left: 2px; }");
    for (int c = 1; c < 4; ++c) tree_->hideColumn(c);      // size / type / date
    tree_->sortByColumn(0, Qt::AscendingOrder);
    connect(tree_, &QTreeView::doubleClicked, this, [this](const QModelIndex& i) { activate(pathAt(i)); });
    connect(tree_, &QTreeView::customContextMenuRequested, this, &FileExplorer::showMenu);

    // header row: FOLDER NAME  [new file] [new folder] [collapse]
    title_ = new QLabel(this);
    title_->setObjectName("explorerTitle");
    title_->setStyleSheet("font-weight:600; padding:4px 6px;");
    auto mkBtn = [this](const QString& text, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setObjectName("explorerBtn");
        return b;
    };
    QToolButton* nf  = mkBtn(QStringLiteral("+"),  tr("New File (Ctrl+N)"));
    QToolButton* nd  = mkBtn(QStringLiteral("⊞"), tr("New Folder"));
    QToolButton* col = mkBtn(QStringLiteral("⊟"), tr("Collapse All"));
    auto* head = new QHBoxLayout;
    head->setContentsMargins(0, 0, 4, 0);
    head->addWidget(title_, 1);
    head->addWidget(nf);
    head->addWidget(nd);
    head->addWidget(col);
    connect(nf, &QToolButton::clicked, this, [this] { if (!root_.isEmpty()) uiNewFile(targetDir(tree_->currentIndex())); });
    connect(nd, &QToolButton::clicked, this, [this] { if (!root_.isEmpty()) uiNewFolder(targetDir(tree_->currentIndex())); });
    connect(col, &QToolButton::clicked, tree_, &QTreeView::collapseAll);

    searchBox_ = new QLineEdit(this);
    searchBox_->setPlaceholderText(tr("Filter files…"));
    searchBox_->setClearButtonEnabled(true);
    searchBox_->setObjectName("explorerSearch");
    searchBox_->hide(); // shown only when folder is open
    connect(searchBox_, &QLineEdit::textChanged, this, &FileExplorer::filterFiles);

    auto* treePage = new QWidget(this);
    auto* tl = new QVBoxLayout(treePage);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(0);
    tl->addLayout(head);
    tl->addWidget(searchBox_);
    tl->addWidget(tree_, 1);

    auto* emptyPage = new QWidget(this);
    auto* el = new QVBoxLayout(emptyPage);
    auto* hint = new QLabel(tr("You have not opened a folder yet."), emptyPage);
    hint->setWordWrap(true);
    auto* openBtn = new QPushButton(tr("Open Folder"), emptyPage);
    el->addWidget(hint);
    el->addWidget(openBtn);
    el->addStretch(1);
    connect(openBtn, &QPushButton::clicked, this, &FileExplorer::openFolderRequested);

    stack_ = new QStackedWidget(this);
    stack_->addWidget(emptyPage);
    stack_->addWidget(treePage);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(stack_);

    // keyboard: Enter opens, F2 renames, Delete deletes
    auto sc = [this](const QKeySequence& k, auto fn) {
        auto* s = new QShortcut(k, tree_);
        s->setContext(Qt::WidgetShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };
    sc(Qt::Key_Return, [this] { activate(pathAt(tree_->currentIndex())); });
    sc(Qt::Key_Enter, [this] { activate(pathAt(tree_->currentIndex())); });
    sc(Qt::Key_F2, [this] { const QString p = pathAt(tree_->currentIndex()); if (!p.isEmpty()) uiRename(p); });
    sc(Qt::Key_Delete, [this] { const QString p = pathAt(tree_->currentIndex()); if (!p.isEmpty()) uiDelete(p); });
    clearRoot();
}

void FileExplorer::applyFilter() {
    QDir::Filters f = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::AllDirs;
    if (showHidden_) f |= QDir::Hidden;
    model_->setFilter(f);
    if (filterText_.isEmpty()) {
        model_->setNameFilters(QStringList());
        model_->setNameFilterDisables(false);
    } else {
        model_->setNameFilters(QStringList() << QStringLiteral("*%1*").arg(filterText_));
        model_->setNameFilterDisables(false);
    }
}

void FileExplorer::filterFiles(const QString& text) {
    filterText_ = text.trimmed();
    applyFilter();
}

bool FileExplorer::setRootPath(const QString& dir, QString* error) {
    const QFileInfo fi(dir);
    if (dir.isEmpty() || !fi.exists()) { if (error) *error = tr("The folder '%1' does not exist.").arg(dir); return false; }
    if (!fi.isDir()) { if (error) *error = tr("'%1' is a file, not a folder.").arg(dir); return false; }
    if (!fi.isReadable() || !QDir(dir).isReadable()) { if (error) *error = tr("No permission to read '%1'.").arg(dir); return false; }
    root_ = QDir::cleanPath(fi.absoluteFilePath());
    model_->setRootPath(root_);
    tree_->setRootIndex(model_->index(root_));
    title_->setText(fi.fileName().isEmpty() ? root_ : fi.fileName().toUpper());
    title_->setToolTip(root_);
    searchBox_->show();
    searchBox_->clear();
    filterText_.clear();
    applyFilter();
    stack_->setCurrentIndex(1);
    emit rootChanged(root_);
    return true;
}

void FileExplorer::clearRoot() {
    root_.clear();
    searchBox_->hide();
    searchBox_->clear();
    filterText_.clear();
    applyFilter();
    stack_->setCurrentIndex(0);
    emit rootChanged(QString());
}

QString FileExplorer::pathAt(const QModelIndex& i) const { return i.isValid() ? model_->filePath(i) : QString(); }

QString FileExplorer::targetDir(const QModelIndex& i) const {
    const QString p = pathAt(i);
    if (p.isEmpty()) return root_;
    return QFileInfo(p).isDir() ? p : QFileInfo(p).absolutePath();
}

void FileExplorer::setCurrentFile(const QString& path) {
    if (root_.isEmpty() || path.isEmpty()) return;
    const QString clean = QDir::cleanPath(path);
    if (!clean.startsWith(root_ + QLatin1Char('/'), Qt::CaseInsensitive)) return;    // outside the folder
    const QModelIndex idx = model_->index(clean);
    if (!idx.isValid()) return;
    for (QModelIndex p = idx.parent(); p.isValid() && p != tree_->rootIndex(); p = p.parent()) tree_->expand(p);
    tree_->setCurrentIndex(idx);
    tree_->scrollTo(idx);
}

void FileExplorer::activate(const QString& path) {
    if (path.isEmpty()) return;
    const QFileInfo fi(path);
    if (fi.isDir()) {
        const QModelIndex i = model_->index(path);
        if (tree_->isExpanded(i)) tree_->collapse(i); else tree_->expand(i);
        return;
    }
    if (!fi.exists()) { emit message(tr("'%1' no longer exists.").arg(path)); return; }
    emit fileActivated(path);
}

// ---------------------------------------------------------------- file operations

bool FileExplorer::validName(const QString& name, QString* why) {
    auto fail = [&](const QString& m) { if (why) *why = m; return false; };
    if (name.trimmed().isEmpty()) return fail(tr("The name cannot be empty."));
    if (name == QLatin1String(".") || name == QLatin1String("..")) return fail(tr("'%1' is not a valid name.").arg(name));
    if (name.size() > 255) return fail(tr("The name is too long (maximum 255 characters)."));
    static const QString bad = QStringLiteral("/\\:*?\"<>|");
    for (const QChar c : name) {
        if (c.unicode() < 32 || bad.contains(c)) return fail(tr("The name cannot contain  / \\ : * ? \" < > |"));
    }
    if (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) return fail(tr("The name cannot end with a dot or a space."));
    const QString stem = name.section(QLatin1Char('.'), 0, 0).toUpper();
    static const QStringList reserved = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
                                         "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    if (reserved.contains(stem)) return fail(tr("'%1' is a reserved name on Windows.").arg(name));
    return true;
}

bool FileExplorer::createFile(const QString& dir, const QString& name, QString* error, QString* createdPath) {
    QString why;
    if (!validName(name, &why)) { if (error) *error = why; return false; }
    if (!QFileInfo(dir).isDir()) { if (error) *error = tr("'%1' is not a folder.").arg(dir); return false; }
    const QString path = QDir(dir).filePath(name);
    if (QFileInfo::exists(path)) { if (error) *error = tr("'%1' already exists.").arg(name); return false; }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (error) *error = tr("Cannot create '%1': %2").arg(name, f.errorString()); return false; }
    f.close();
    if (createdPath) *createdPath = path;
    return true;
}

bool FileExplorer::createFolder(const QString& dir, const QString& name, QString* error, QString* createdPath) {
    QString why;
    if (!validName(name, &why)) { if (error) *error = why; return false; }
    if (!QFileInfo(dir).isDir()) { if (error) *error = tr("'%1' is not a folder.").arg(dir); return false; }
    const QString path = QDir(dir).filePath(name);
    if (QFileInfo::exists(path)) { if (error) *error = tr("'%1' already exists.").arg(name); return false; }
    if (!QDir(dir).mkdir(name)) { if (error) *error = tr("Cannot create the folder '%1' (permission?).").arg(name); return false; }
    if (createdPath) *createdPath = path;
    return true;
}

bool FileExplorer::renamePath(const QString& path, const QString& newName, QString* error, QString* newPath) {
    QString why;
    if (!validName(newName, &why)) { if (error) *error = why; return false; }
    const QFileInfo fi(path);
    if (!fi.exists()) { if (error) *error = tr("'%1' no longer exists.").arg(path); return false; }
    if (path == QDir::cleanPath(root_)) { if (error) *error = tr("The opened folder itself cannot be renamed here."); return false; }
    const QString target = QDir(fi.absolutePath()).filePath(newName);
    if (target == QDir::cleanPath(fi.absoluteFilePath())) { if (newPath) *newPath = target; return true; }   // same name
    if (QFileInfo::exists(target)) { if (error) *error = tr("'%1' already exists.").arg(newName); return false; }
    if (!QDir().rename(fi.absoluteFilePath(), target)) {
        if (error) *error = tr("Cannot rename '%1' (is it open in another program?).").arg(fi.fileName());
        return false;
    }
    if (newPath) *newPath = target;
    return true;
}

bool FileExplorer::removePath(const QString& path, bool toTrash, QString* error) {
    const QFileInfo fi(path);
    if (!fi.exists() && !fi.isSymLink()) { if (error) *error = tr("'%1' no longer exists.").arg(path); return false; }
    if (QDir::cleanPath(fi.absoluteFilePath()) == QDir::cleanPath(root_)) {
        if (error) *error = tr("The opened folder itself cannot be deleted here.");
        return false;
    }
    if (toTrash && QFile::moveToTrash(path)) return true;
    const bool ok = fi.isDir() && !fi.isSymLink() ? QDir(path).removeRecursively() : QFile::remove(path);
    if (!ok && error) *error = tr("Cannot delete '%1' (read-only or in use?).").arg(fi.fileName());
    return ok;
}

// ---------------------------------------------------------------- dialogs + menu

void FileExplorer::uiNewFile(const QString& dir) {
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New File"), tr("File name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok) return;
    QString err, created;
    if (!createFile(dir, name.trimmed(), &err, &created)) { QMessageBox::warning(this, tr("New File"), err); return; }
    setCurrentFile(created);
    emit fileActivated(created);
}

void FileExplorer::uiNewFolder(const QString& dir) {
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok) return;
    QString err, created;
    if (!createFolder(dir, name.trimmed(), &err, &created)) { QMessageBox::warning(this, tr("New Folder"), err); return; }
    setCurrentFile(created);
}

void FileExplorer::uiRename(const QString& path) {
    bool ok = false;
    const QString old = QFileInfo(path).fileName();
    const QString name = QInputDialog::getText(this, tr("Rename"), tr("New name:"), QLineEdit::Normal, old, &ok);
    if (!ok || name.trimmed() == old) return;
    QString err, np;
    if (!renamePath(path, name.trimmed(), &err, &np)) { QMessageBox::warning(this, tr("Rename"), err); return; }
    emit fileRenamed(QDir::cleanPath(path), np);
}

void FileExplorer::uiDelete(const QString& path) {
    const QFileInfo fi(path);
    const auto b = QMessageBox::question(this, tr("Delete"),
                                         tr("Move '%1'%2 to the Trash / Recycle Bin?").arg(fi.fileName(), fi.isDir() ? tr(" and everything in it") : QString()),
                                         QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (b != QMessageBox::Yes) return;
    QString err;
    if (!removePath(path, true, &err)) { QMessageBox::warning(this, tr("Delete"), err); return; }
    emit pathRemoved(QDir::cleanPath(path));
}

void FileExplorer::showMenu(const QPoint& pos) {
    if (root_.isEmpty()) return;
    const QModelIndex idx = tree_->indexAt(pos);
    const QString path = pathAt(idx);
    const QString dir = targetDir(idx);
    QMenu m(this);
    if (!path.isEmpty() && !QFileInfo(path).isDir()) m.addAction(tr("Open"), this, [this, path] { activate(path); });
    m.addAction(tr("New File..."), this, [this, dir] { uiNewFile(dir); });
    m.addAction(tr("New Folder..."), this, [this, dir] { uiNewFolder(dir); });
    if (!path.isEmpty()) {
        m.addSeparator();
        m.addAction(tr("Rename...\tF2"), this, [this, path] { uiRename(path); });
        m.addAction(tr("Delete\tDel"), this, [this, path] { uiDelete(path); });
        m.addSeparator();
        m.addAction(tr("Copy Path"), this, [path] { QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(path)); });
        m.addAction(tr("Show in File Manager"), this, [path] {
            const QFileInfo fi(path);
            QDesktopServices::openUrl(QUrl::fromLocalFile(fi.isDir() ? path : fi.absolutePath()));
        });
    }
    m.addSeparator();
    QAction* h = m.addAction(tr("Show Hidden Files"));
    h->setCheckable(true);
    h->setChecked(showHidden_);
    connect(h, &QAction::toggled, this, [this](bool on) { showHidden_ = on; applyFilter(); });
    m.addAction(tr("Refresh"), this, [this] { const QString r = root_; model_->setRootPath(QString()); model_->setRootPath(r); tree_->setRootIndex(model_->index(r)); });
    m.addAction(tr("Close Folder"), this, [this] { clearRoot(); });
    m.exec(tree_->viewport()->mapToGlobal(pos));
}
