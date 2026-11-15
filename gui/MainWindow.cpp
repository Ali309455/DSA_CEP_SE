#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

#include "CodeView.h"
#include "Workspace.h"

static const char* kApp = "DevPad";

// ---------------------------------------------------------------- construction

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(kApp);
    setAcceptDrops(true);
    resize(1200, 780);

    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    tabs_->setUsesScrollButtons(true);
    tabs_->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    tabs_->tabBar()->setExpanding(false);
    connect(tabs_, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int i) { closeTab(i); });
    connect(tabs_->tabBar(), &QTabBar::tabBarDoubleClicked, this, [this](int i) { if (i < 0) newTab(); });
    connect(tabs_->tabBar(), &QWidget::customContextMenuRequested, this, [this](const QPoint& pt) {
        const int i = tabs_->tabBar()->tabAt(pt);
        if (i < 0) return;
        QMenu m;
        QAction* close = m.addAction("Close");
        QAction* others = m.addAction("Close Others");
        m.addSeparator();
        QAction* rename = m.addAction("Rename File…");
        QAction* copyPath = m.addAction("Copy Path");
        QAction* chosen = m.exec(tabs_->tabBar()->mapToGlobal(pt));
        if (!chosen) return;
        if (chosen == close) closeTab(i);
        else if (chosen == others) {
            CodeView* keep = viewAt(i);
            for (int k = tabs_->count() - 1; k >= 0; --k)
                if (viewAt(k) != keep && !closeTab(k)) break;
        } else if (chosen == rename) { tabs_->setCurrentIndex(i); renameDialog(); }
        else if (chosen == copyPath) QApplication::clipboard()->setText(QString::fromStdString(viewAt(i)->editor().doc().path()));
    });

    buildFindBar();
    QWidget* central = new QWidget(this);
    QVBoxLayout* lay = new QVBoxLayout(central);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(tabs_, 1);
    lay->addWidget(findBar_);
    setCentralWidget(central);

    // sidebar: file tree
    fsModel_ = new QFileSystemModel(this);
    fsModel_->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    tree_ = new QTreeView(this);
    tree_->setModel(fsModel_);
    tree_->setHeaderHidden(true);
    for (int c = 1; c < 4; ++c) tree_->hideColumn(c);
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(tree_, &QTreeView::activated, this, &MainWindow::onTreeActivated);
    dock_ = new QDockWidget("Explorer", this);
    dock_->setObjectName("explorerDock");
    dock_->setWidget(tree_);
    dock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::LeftDockWidgetArea, dock_);
    dock_->hide();

    // status bar
    posLabel_ = new QLabel("Ln 1, Col 1");
    langLabel_ = new QLabel();
    eolLabel_ = new QLabel("LF");
    issueBtn_ = new QPushButton("✓ Brackets OK");
    issueBtn_->setFlat(true);
    issueBtn_->setCursor(Qt::PointingHandCursor);
    connect(issueBtn_, &QPushButton::clicked, this, [this]() {
        CodeView* v = currentView();
        Pos p;
        if (v && v->firstBracketIssuePos(p)) { v->setCursor(p); v->setFocus(); }
    });
    statusBar()->addPermanentWidget(issueBtn_);
    statusBar()->addPermanentWidget(posLabel_);
    statusBar()->addPermanentWidget(langLabel_);
    statusBar()->addPermanentWidget(new QLabel("UTF-8"));
    statusBar()->addPermanentWidget(eolLabel_);

    buildActions();
    applyDarkTheme();
    newTab();
}

void MainWindow::applyDarkTheme() {
    qApp->setStyle("Fusion");
    QPalette p;
    p.setColor(QPalette::Window, QColor(0x25, 0x25, 0x26));
    p.setColor(QPalette::WindowText, QColor(0xcc, 0xcc, 0xcc));
    p.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));
    p.setColor(QPalette::AlternateBase, QColor(0x25, 0x25, 0x26));
    p.setColor(QPalette::Text, QColor(0xd4, 0xd4, 0xd4));
    p.setColor(QPalette::Button, QColor(0x33, 0x33, 0x33));
    p.setColor(QPalette::ButtonText, QColor(0xcc, 0xcc, 0xcc));
    p.setColor(QPalette::Highlight, QColor(0x09, 0x47, 0x71));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, QColor(0x25, 0x25, 0x26));
    p.setColor(QPalette::ToolTipText, QColor(0xcc, 0xcc, 0xcc));
    p.setColor(QPalette::PlaceholderText, QColor(0x80, 0x80, 0x80));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x6e, 0x6e, 0x6e));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x6e, 0x6e, 0x6e));
    qApp->setPalette(p);
    qApp->setStyleSheet(
        "QTabBar::tab{background:#2d2d2d;color:#969696;padding:7px 14px;border-right:1px solid #252526;min-width:60px;}"
        "QTabBar::tab:selected{background:#1e1e1e;color:#ffffff;border-top:2px solid #0e7ad1;}"
        "QTabBar::tab:hover:!selected{background:#323233;}"
        "QTabWidget::pane{border:0;}"
        "QStatusBar{background:#007acc;color:#ffffff;}"
        "QStatusBar QLabel,QStatusBar QPushButton{color:#ffffff;padding:0 8px;background:transparent;border:0;}"
        "QDockWidget{color:#bbbbbb;font-weight:bold;}"
        "QDockWidget::title{background:#252526;padding:6px;}"
        "QTreeView{background:#252526;border:0;color:#cccccc;}"
        "QTreeView::item:selected{background:#094771;}"
        "QMenuBar{background:#333333;color:#cccccc;}"
        "QMenuBar::item:selected{background:#505050;}"
        "QMenu{background:#252526;color:#cccccc;border:1px solid #454545;}"
        "QMenu::item:selected{background:#094771;}"
        "QLineEdit{background:#3c3c3c;color:#cccccc;border:1px solid #3c3c3c;padding:3px 6px;border-radius:2px;}"
        "QLineEdit:focus{border:1px solid #0e7ad1;}"
        "QPushButton{background:#3c3c3c;color:#cccccc;border:1px solid #454545;padding:3px 10px;border-radius:2px;}"
        "QPushButton:hover{background:#505050;}"
        "QCheckBox{color:#cccccc;}"
        "QDialog{background:#252526;}"
        "QListWidget{background:#252526;color:#cccccc;border:1px solid #454545;}"
        "QListWidget::item:selected{background:#094771;color:#ffffff;}");
}

// ---------------------------------------------------------------- actions and shortcuts

void MainWindow::buildActions() {
    auto add = [this](QMenu* menu, const QString& text, const QList<QKeySequence>& keys, std::function<void()> fn) {
        QAction* a = menu->addAction(text);
        if (!keys.isEmpty()) a->setShortcuts(keys);
        connect(a, &QAction::triggered, this, [fn]() { fn(); });
        addAction(a);                                        // shortcuts work even if the menu bar is hidden
        return a;
    };
    auto seq = [](const char* s) { return QList<QKeySequence>{QKeySequence(QString::fromLatin1(s))}; };
    auto onView = [this](void (CodeView::*fn)()) {
        return [this, fn]() { if (CodeView* v = currentView()) (v->*fn)(); };
    };

    QMenu* file = menuBar()->addMenu("&File");
    add(file, "New File", {QKeySequence::New}, [this]() { newTab(); });
    add(file, "Open File…", {QKeySequence::Open}, [this]() { openDialog(); });
    add(file, "Open Folder…", seq("Ctrl+Shift+O"), [this]() { openFolderDialog(); });
    file->addSeparator();
    add(file, "Save", {QKeySequence::Save}, [this]() { saveCurrent(); });
    add(file, "Save As…", seq("Ctrl+Shift+S"), [this]() { saveCurrentAs(); });
    add(file, "Save All", seq("Ctrl+Alt+S"), [this]() { saveAll(); });
    file->addSeparator();
    add(file, "Rename File…", seq("F2"), [this]() { renameDialog(); });
    add(file, "Close Tab", seq("Ctrl+W"), [this]() { closeTab(tabs_->currentIndex()); });
    file->addSeparator();
    add(file, "Quit", seq("Ctrl+Q"), [this]() { close(); });

    QMenu* edit = menuBar()->addMenu("&Edit");
    add(edit, "Undo", {QKeySequence::Undo}, onView(&CodeView::undo));
    add(edit, "Redo", {QKeySequence(QStringLiteral("Ctrl+Y")), QKeySequence(QStringLiteral("Ctrl+Shift+Z"))}, onView(&CodeView::redo));
    edit->addSeparator();
    add(edit, "Cut", {QKeySequence::Cut}, onView(&CodeView::cut));
    add(edit, "Copy", {QKeySequence::Copy}, onView(&CodeView::copy));
    add(edit, "Paste", {QKeySequence::Paste}, onView(&CodeView::paste));
    add(edit, "Select All", {QKeySequence::SelectAll}, onView(&CodeView::selectAll));
    add(edit, "Select Line", seq("Ctrl+L"), onView(&CodeView::selectLine));
    edit->addSeparator();
    QMenu* lines = edit->addMenu("Lines");
    add(lines, "Move Line Up", seq("Alt+Up"), onView(&CodeView::moveLinesUp));
    add(lines, "Move Line Down", seq("Alt+Down"), onView(&CodeView::moveLinesDown));
    add(lines, "Copy Line Up", seq("Shift+Alt+Up"), onView(&CodeView::copyLinesUp));
    add(lines, "Copy Line Down", {QKeySequence(QStringLiteral("Shift+Alt+Down")), QKeySequence(QStringLiteral("Ctrl+D"))}, onView(&CodeView::copyLinesDown));
    add(lines, "Delete Line", seq("Ctrl+Shift+K"), onView(&CodeView::deleteLine));
    add(lines, "Insert Line Below", {QKeySequence(QStringLiteral("Ctrl+Return")), QKeySequence(QStringLiteral("Ctrl+Enter"))}, onView(&CodeView::newLineBelow));
    add(lines, "Insert Line Above", {QKeySequence(QStringLiteral("Ctrl+Shift+Return")), QKeySequence(QStringLiteral("Ctrl+Shift+Enter"))}, onView(&CodeView::newLineAbove));
    add(lines, "Indent", seq("Ctrl+]"), onView(&CodeView::indentSelection));
    add(lines, "Outdent", seq("Ctrl+["), onView(&CodeView::outdentSelection));
    add(edit, "Toggle Line Comment", seq("Ctrl+/"), onView(&CodeView::toggleComment));
    add(edit, "Trigger Autocomplete", seq("Ctrl+Space"), onView(&CodeView::triggerAutocomplete));

    QMenu* search = menuBar()->addMenu("&Search");
    add(search, "Find…", {QKeySequence::Find}, [this]() { showFind(false); });
    add(search, "Replace…", seq("Ctrl+H"), [this]() { showFind(true); });
    add(search, "Find Next", {QKeySequence(QStringLiteral("F3"))}, [this]() { findNext(false); });
    add(search, "Find Previous", {QKeySequence(QStringLiteral("Shift+F3"))}, [this]() { findNext(true); });
    search->addSeparator();
    add(search, "Go to File…", {QKeySequence(QStringLiteral("Ctrl+P"))}, [this]() { quickOpen(); });
    add(search, "Go to Line…", seq("Ctrl+G"), [this]() { goToLineDialog(); });

    QMenu* view = menuBar()->addMenu("&View");
    add(view, "Next Tab", {QKeySequence(QStringLiteral("Ctrl+Tab")), QKeySequence(QStringLiteral("Ctrl+PgDown"))}, [this]() { nextTab(+1); });
    add(view, "Previous Tab", {QKeySequence(QStringLiteral("Ctrl+Shift+Tab")), QKeySequence(QStringLiteral("Ctrl+PgUp"))}, [this]() { nextTab(-1); });
    for (int i = 1; i <= 9; ++i) {                                         // Alt+1 .. Alt+9; Alt+9 is the last tab
        QAction* a = new QAction(this);
        a->setShortcut(QKeySequence(QString("Alt+%1").arg(i)));
        connect(a, &QAction::triggered, this, [this, i]() {
            const int n = tabs_->count();
            if (n > 0) tabs_->setCurrentIndex(i == 9 ? n - 1 : std::min(i - 1, n - 1));
        });
        addAction(a);
    }
    view->addSeparator();
    QAction* side = dock_->toggleViewAction();
    side->setText("Toggle Sidebar");
    side->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    view->addAction(side);
    addAction(side);
    auto zoomBy = [this](int d) {
        for (int i = 0; i < tabs_->count(); ++i) viewAt(i)->setZoom(d == 0 ? 11 : viewAt(i)->zoom() + d);
    };
    add(view, "Zoom In", {QKeySequence(QStringLiteral("Ctrl+=")), QKeySequence(QStringLiteral("Ctrl++"))}, [zoomBy]() { zoomBy(+1); });
    add(view, "Zoom Out", seq("Ctrl+-"), [zoomBy]() { zoomBy(-1); });
    add(view, "Reset Zoom", seq("Ctrl+0"), [zoomBy]() { zoomBy(0); });

    QMenu* help = menuBar()->addMenu("&Help");
    add(help, "Keyboard Shortcuts", seq("F1"), [this]() {
        QMessageBox::information(this, "Keyboard shortcuts",
            "Tabs\n"
            "  Ctrl+Tab / Ctrl+Shift+Tab   next / previous tab\n"
            "  Ctrl+PgDn / Ctrl+PgUp       next / previous tab\n"
            "  Alt+1 … Alt+9               go to tab (Alt+9 = last)\n"
            "  Ctrl+N / Ctrl+W             new / close tab\n\n"
            "Files\n"
            "  Ctrl+P                      go to file (fuzzy search)\n"
            "  F2                          rename file\n"
            "  Ctrl+Shift+O                open folder\n"
            "  Ctrl+B                      toggle sidebar\n\n"
            "Lines\n"
            "  Alt+Up / Alt+Down           move line(s)\n"
            "  Shift+Alt+Up / Down         copy line(s) up / down  (Ctrl+D also copies down)\n"
            "  Ctrl+Shift+K                delete line\n"
            "  Ctrl+Enter / Ctrl+Shift+Enter   new line below / above\n"
            "  Ctrl+] / Ctrl+[             indent / outdent\n"
            "  Ctrl+/                      toggle comment\n"
            "  Ctrl+L                      select line\n\n"
            "Brackets and quotes\n"
            "  type ( [ { \" ' ` with text selected   wrap the selection\n"
            "  type ( [ { \" ' `                      auto-closing pair\n\n"
            "Search\n"
            "  Ctrl+F / Ctrl+H             find / replace\n"
            "  F3 / Shift+F3               next / previous match\n"
            "  Ctrl+G                      go to line\n"
            "  Ctrl+Space                  autocomplete");
    });
}

// ---------------------------------------------------------------- find bar

void MainWindow::buildFindBar() {
    findBar_ = new QWidget(this);
    findBar_->setStyleSheet("QWidget#findBar{background:#252526;border-top:1px solid #3c3c3c;}");
    findBar_->setObjectName("findBar");
    QVBoxLayout* v = new QVBoxLayout(findBar_);
    v->setContentsMargins(8, 6, 8, 6);
    v->setSpacing(4);

    QHBoxLayout* r1 = new QHBoxLayout();
    find_ = new QLineEdit();
    find_->setPlaceholderText("Find");
    caseBox_ = new QCheckBox("Aa");
    caseBox_->setToolTip("Match case");
    wordBox_ = new QCheckBox("Word");
    wordBox_->setToolTip("Whole word");
    findCount_ = new QLabel();
    findCount_->setMinimumWidth(90);
    QPushButton* prev = new QPushButton("▲");
    QPushButton* next = new QPushButton("▼");
    QPushButton* close = new QPushButton("✕");
    prev->setToolTip("Previous match (Shift+F3)");
    next->setToolTip("Next match (F3)");
    for (QPushButton* b : {prev, next, close}) b->setFixedWidth(30);
    r1->addWidget(find_, 1);
    r1->addWidget(caseBox_);
    r1->addWidget(wordBox_);
    r1->addWidget(findCount_);
    r1->addWidget(prev);
    r1->addWidget(next);
    r1->addWidget(close);
    v->addLayout(r1);

    replRow_ = new QWidget();
    QHBoxLayout* r2 = new QHBoxLayout(replRow_);
    r2->setContentsMargins(0, 0, 0, 0);
    repl_ = new QLineEdit();
    repl_->setPlaceholderText("Replace");
    QPushButton* one = new QPushButton("Replace");
    QPushButton* all = new QPushButton("Replace All");
    r2->addWidget(repl_, 1);
    r2->addWidget(one);
    r2->addWidget(all);
    v->addWidget(replRow_);

    connect(find_, &QLineEdit::textChanged, this, &MainWindow::updateFind);
    connect(find_, &QLineEdit::returnPressed, this, [this]() { findNext(false); });
    connect(repl_, &QLineEdit::returnPressed, this, &MainWindow::replaceOne);
    connect(caseBox_, &QCheckBox::toggled, this, &MainWindow::updateFind);
    connect(wordBox_, &QCheckBox::toggled, this, &MainWindow::updateFind);
    connect(prev, &QPushButton::clicked, this, [this]() { findNext(true); });
    connect(next, &QPushButton::clicked, this, [this]() { findNext(false); });
    connect(close, &QPushButton::clicked, this, &MainWindow::closeFind);
    connect(one, &QPushButton::clicked, this, &MainWindow::replaceOne);
    connect(all, &QPushButton::clicked, this, &MainWindow::replaceEverything);

    QShortcut* esc = new QShortcut(QKeySequence(Qt::Key_Escape), findBar_);
    esc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(esc, &QShortcut::activated, this, &MainWindow::closeFind);
    QShortcut* back = new QShortcut(QKeySequence(QStringLiteral("Shift+Return")), find_);
    back->setContext(Qt::WidgetShortcut);
    connect(back, &QShortcut::activated, this, [this]() { findNext(true); });
    findBar_->hide();
}

SearchOptions MainWindow::searchOptions() const {
    SearchOptions o;
    o.matchCase = caseBox_->isChecked();
    o.wholeWord = wordBox_->isChecked();
    return o;
}

void MainWindow::showFind(bool replace) {
    CodeView* v = currentView();
    if (!v) return;
    findBar_->show();
    replRow_->setVisible(replace);
    const QString sel = v->selectedText();
    if (!sel.isEmpty() && !sel.contains(QLatin1Char('\n'))) find_->setText(sel);
    find_->setFocus();
    find_->selectAll();
    updateFind();
}

void MainWindow::closeFind() {
    findBar_->hide();
    matches_.clear();
    if (CodeView* v = currentView()) { v->clearSearchHighlights(); v->setFocus(); }
}

void MainWindow::highlightAll() {
    CodeView* v = currentView();
    matches_.clear();
    if (!v) return;
    const std::string pat = find_->text().toUtf8().toStdString();
    if (!findBar_->isVisible() || pat.empty()) {
        v->clearSearchHighlights();
        findCount_->setText(QString());
        return;
    }
    DynArray<Match> all = findAll(v->editor().doc(), pat, searchOptions(), false);
    matches_.reserve(all.size());
    for (size_t i = 0; i < all.size(); ++i) matches_.push_back(all[i]);
    v->setSearchHighlights(matches_);
    findCount_->setText(matches_.empty() ? QStringLiteral("No results") : QString("%1 found").arg(matches_.size()));
    findCount_->setStyleSheet(matches_.empty() ? "color:#f48771;" : "");
}

void MainWindow::updateFind() {
    highlightAll();
    if (!matches_.empty()) runFind(false, true);
}

void MainWindow::runFind(bool backwards, bool fromSelectionStart) {
    CodeView* v = currentView();
    if (!v || matches_.empty()) return;
    size_t idx = 0;
    if (!backwards) {
        const Pos from = fromSelectionStart ? v->selStart() : v->selEnd();
        idx = matches_.size();                                  // first match starting at / after 'from'; wraps to 0
        for (size_t lo = 0, hi = matches_.size(); lo < hi;) {
            size_t mid = (lo + hi) / 2;
            if (matches_[mid].start < from) lo = mid + 1; else hi = mid;
            idx = lo;
        }
        if (idx >= matches_.size()) idx = 0;
    } else {
        const Pos from = v->selStart();
        size_t lo = 0, hi = matches_.size();
        while (lo < hi) { size_t mid = (lo + hi) / 2; if (matches_[mid].start < from) lo = mid + 1; else hi = mid; }
        idx = lo == 0 ? matches_.size() - 1 : lo - 1;           // last match before the selection; wraps to the end
    }
    v->select(matches_[idx].start, matches_[idx].end);
    findCount_->setText(QString("%1 of %2").arg(idx + 1).arg(matches_.size()));
    findCount_->setStyleSheet(QString());
}

void MainWindow::findNext(bool backwards) {
    if (!findBar_->isVisible()) { showFind(false); return; }
    if (matches_.empty()) highlightAll();
    runFind(backwards, false);
}

void MainWindow::replaceOne() {
    CodeView* v = currentView();
    const std::string pat = find_->text().toUtf8().toStdString();
    if (!v || pat.empty()) return;
    const std::string rep = repl_->text().toUtf8().toStdString();
    Pos after;
    const Pos from = v->selStart();
    if (v->editor().replaceNext(from, pat, rep, searchOptions(), &after, true)) {
        v->setCursor(after);
        v->notifyEdited(from.line);
    }
    highlightAll();
    if (!matches_.empty()) runFind(false, false);
}

void MainWindow::replaceEverything() {
    CodeView* v = currentView();
    const std::string pat = find_->text().toUtf8().toStdString();
    if (!v || pat.empty()) return;
    const std::string rep = repl_->text().toUtf8().toStdString();
    const ReplaceResult r = v->editor().replaceAll(pat, rep, searchOptions());
    v->notifyEdited(0);
    highlightAll();
    findCount_->setText(QString("%1 replaced").arg(r.changed));
}

// ---------------------------------------------------------------- tabs

CodeView* MainWindow::currentView() const { return qobject_cast<CodeView*>(tabs_->currentWidget()); }
int MainWindow::tabCount() const { return tabs_->count(); }
CodeView* MainWindow::viewAt(int i) const { return qobject_cast<CodeView*>(tabs_->widget(i)); }
QString MainWindow::tabTitle(int i) const { return tabs_->tabText(i); }

void MainWindow::connectView(CodeView* v) {
    connect(v, &CodeView::stateChanged, this, &MainWindow::onViewStateChanged);
    connect(v, &CodeView::textEdited, this, [this, v]() {
        if (v == currentView() && findBar_->isVisible()) highlightAll();
    });
}

CodeView* MainWindow::newTab(const QString& path) {
    CodeView* v = new CodeView();
    if (!path.isEmpty()) {
        QString err;
        if (!v->openFile(path, &err)) {
            QMessageBox::warning(this, kApp, err);
            delete v;
            return nullptr;
        }
    } else {
        v->setProperty("untitledName", QString("Untitled-%1").arg(++untitled_));
    }
    connectView(v);
    const int i = tabs_->addTab(v, QString());
    tabs_->setCurrentIndex(i);
    refreshTab(v);
    v->setFocus();
    return v;
}

int MainWindow::indexOfPath(const QString& path) const {
    const QString want = QFileInfo(path).canonicalFilePath();
    if (want.isEmpty()) return -1;
    for (int i = 0; i < tabs_->count(); ++i) {
        const QString p = QString::fromStdString(viewAt(i)->editor().doc().path());
        if (!p.isEmpty() && QFileInfo(p).canonicalFilePath() == want) return i;
    }
    return -1;
}

bool MainWindow::openPath(const QString& path) {
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        QMessageBox::warning(this, kApp, "Cannot open '" + path + "': the file does not exist.");
        return false;
    }
    const int existing = indexOfPath(path);
    if (existing >= 0) { tabs_->setCurrentIndex(existing); return true; }
    if (root_.isEmpty()) setWorkspaceRoot(fi.absolutePath());
    // a single untouched "Untitled" tab is replaced instead of left behind
    CodeView* cur = currentView();
    if (tabs_->count() == 1 && cur && cur->editor().doc().path().empty() &&
        !cur->editor().doc().modified() && cur->editor().doc().isEmpty()) {
        QString err;
        if (!cur->openFile(fi.absoluteFilePath(), &err)) { QMessageBox::warning(this, kApp, err); return false; }
        refreshTab(cur);
        onTabChanged(0);
        return true;
    }
    return newTab(fi.absoluteFilePath()) != nullptr;
}

void MainWindow::refreshTab(CodeView* v) {
    const int i = tabs_->indexOf(v);
    if (i < 0) return;
    const QString path = QString::fromStdString(v->editor().doc().path());
    QString name = path.isEmpty() ? v->property("untitledName").toString() : QFileInfo(path).fileName();
    if (v->editor().doc().modified()) name += QStringLiteral(" ●");
    tabs_->setTabText(i, name);
    tabs_->setTabToolTip(i, path.isEmpty() ? name : QDir::toNativeSeparators(path));
}

void MainWindow::refreshTitle() {
    CodeView* v = currentView();
    if (!v) { setWindowTitle(kApp); return; }
    const QString path = QString::fromStdString(v->editor().doc().path());
    QString name = path.isEmpty() ? v->property("untitledName").toString() : QFileInfo(path).fileName();
    if (v->editor().doc().modified()) name += "*";
    setWindowTitle(name + " — " + kApp);
}

void MainWindow::onTabChanged(int) {
    CodeView* v = currentView();
    if (!v) return;
    refreshTitle();
    onViewStateChanged();
    if (findBar_->isVisible()) highlightAll();
    v->setFocus();
}

void MainWindow::onViewStateChanged() {
    CodeView* s = qobject_cast<CodeView*>(sender());
    if (s) refreshTab(s);
    CodeView* v = currentView();
    if (!v || (s && s != v)) return;
    refreshTitle();
    const Pos c = v->cursor();
    QString pos = QString("Ln %1, Col %2").arg(c.line + 1).arg(v->displayColumn(c) + 1);
    if (v->hasSelection()) pos += QString(" (%1 selected)").arg(v->selectedText().size());
    posLabel_->setText(pos);
    langLabel_->setText(v->languageName());
    eolLabel_->setText(v->editor().doc().crlf() ? "CRLF" : "LF");
    if (v->bracketsReady()) {
        const size_t n = v->bracketIssueCount();
        issueBtn_->setText(n == 0 ? QString("✓ Brackets OK") : QString("⚠ %1 bracket issue%2").arg(n).arg(n == 1 ? "" : "s"));
        issueBtn_->setToolTip(n == 0 ? QString() : v->firstBracketIssue());
    }
}

void MainWindow::nextTab(int step) {
    const int n = tabs_->count();
    if (n < 2) return;
    tabs_->setCurrentIndex(((tabs_->currentIndex() + step) % n + n) % n);
}

bool MainWindow::maybeSave(CodeView* v) {
    if (!v->editor().doc().modified()) return true;
    tabs_->setCurrentWidget(v);
    refreshTab(v);
    const QString name = tabs_->tabText(tabs_->indexOf(v)).remove(QStringLiteral(" ●"));
    QMessageBox box(QMessageBox::Warning, kApp, "Do you want to save the changes to " + name + "?",
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    box.setDefaultButton(QMessageBox::Save);
    switch (box.exec()) {
        case QMessageBox::Save: return saveView(v, false);
        case QMessageBox::Discard: return true;
        default: return false;
    }
}

bool MainWindow::closeTab(int index, bool force) {
    CodeView* v = viewAt(index);
    if (!v) return false;
    if (!force && !maybeSave(v)) return false;
    tabs_->removeTab(index);
    delete v;
    if (tabs_->count() == 0) newTab();
    return true;
}

// ---------------------------------------------------------------- files

bool MainWindow::saveView(CodeView* v, bool forceDialog) {
    QString path = QString::fromStdString(v->editor().doc().path());
    if (path.isEmpty() || forceDialog) {
        const QString start = path.isEmpty() ? QDir(root_.isEmpty() ? QDir::currentPath() : root_).filePath(v->property("untitledName").toString() + ".txt") : path;
        path = QFileDialog::getSaveFileName(this, "Save As", start);
        if (path.isEmpty()) return false;
    }
    QString err;
    if (!v->saveFile(path, &err)) {
        QMessageBox::warning(this, kApp, err);
        return false;
    }
    if (root_.isEmpty()) setWorkspaceRoot(QFileInfo(path).absolutePath());
    refreshTab(v);
    refreshTitle();
    statusBar()->showMessage("Saved " + QDir::toNativeSeparators(path), 3000);
    return true;
}

void MainWindow::saveCurrent() { if (CodeView* v = currentView()) saveView(v, false); }
void MainWindow::saveCurrentAs() { if (CodeView* v = currentView()) saveView(v, true); }

void MainWindow::saveAll() {
    for (int i = 0; i < tabs_->count(); ++i)
        if (viewAt(i)->editor().doc().modified() && !saveView(viewAt(i), false)) return;
}

void MainWindow::openDialog() {
    const QStringList files = QFileDialog::getOpenFileNames(this, "Open File", root_);
    for (const QString& f : files) openPath(f);
}

void MainWindow::openFolderDialog() {
    const QString d = QFileDialog::getExistingDirectory(this, "Open Folder", root_);
    if (!d.isEmpty()) { setWorkspaceRoot(d); dock_->show(); }
}

void MainWindow::setWorkspaceRoot(const QString& dir) {
    const QFileInfo fi(dir);
    if (!fi.isDir()) return;
    root_ = fi.canonicalFilePath();
    rootChosen_ = true;
    fsModel_->setRootPath(root_);
    tree_->setRootIndex(fsModel_->index(root_));
    dock_->setWindowTitle(fi.fileName().isEmpty() ? root_ : fi.fileName().toUpper());
}

void MainWindow::onTreeActivated(const QModelIndex& idx) {
    const QString p = fsModel_->filePath(idx);
    if (QFileInfo(p).isFile()) openPath(p);
}

void MainWindow::goToLineDialog() {
    CodeView* v = currentView();
    if (!v) return;
    bool ok = false;
    const int n = static_cast<int>(v->editor().doc().lineCount());
    const int l = QInputDialog::getInt(this, "Go to Line", QString("Line number (1 – %1):").arg(n),
                                       static_cast<int>(v->cursor().line) + 1, 1, n, 1, &ok);
    if (ok) { v->goToLine(l); v->setFocus(); }
}

// ---------------------------------------------------------------- rename

bool MainWindow::renameCurrentFile(const QString& newName, QString* error) {
    CodeView* v = currentView();
    if (!v) return false;
    IOResult r = v->editor().renameFile(newName.toUtf8().constData());
    if (!r.ok) {
        if (error) *error = QString::fromStdString(r.message);
        return false;
    }
    v->setLanguageForPath(QString::fromStdString(v->editor().doc().path()));
    refreshTab(v);
    refreshTitle();
    onViewStateChanged();
    return true;
}

void MainWindow::renameDialog() {
    CodeView* v = currentView();
    if (!v) return;
    const QString path = QString::fromStdString(v->editor().doc().path());
    if (path.isEmpty()) {
        if (QMessageBox::question(this, "Rename File", "This file has not been saved yet. Save it first?") == QMessageBox::Yes)
            saveView(v, true);
        return;
    }
    bool ok = false;
    const QString oldName = QFileInfo(path).fileName();
    const QString name = QInputDialog::getText(this, "Rename File", "New name:", QLineEdit::Normal, oldName, &ok);
    if (!ok || name.isEmpty() || name == oldName) return;
    QString err;
    if (!renameCurrentFile(name, &err)) QMessageBox::warning(this, "Rename File", err);
    else statusBar()->showMessage("Renamed to " + name, 3000);
}

// ---------------------------------------------------------------- quick open

namespace {
// Lets Up / Down / PageUp / PageDown in the search field move the selection of the result list.
class ListKeys : public QObject {
public:
    ListKeys(QListWidget* l, QObject* parent) : QObject(parent), list_(l) {}
protected:
    bool eventFilter(QObject*, QEvent* e) override {
        if (e->type() != QEvent::KeyPress) return false;
        const int k = static_cast<QKeyEvent*>(e)->key();
        const int n = list_->count();
        if (n == 0) return false;
        int row = list_->currentRow();
        if (k == Qt::Key_Down) row = std::min(row + 1, n - 1);
        else if (k == Qt::Key_Up) row = std::max(row - 1, 0);
        else if (k == Qt::Key_PageDown) row = std::min(row + 8, n - 1);
        else if (k == Qt::Key_PageUp) row = std::max(row - 8, 0);
        else return false;
        list_->setCurrentRow(row);
        return true;
    }
private:
    QListWidget* list_;
};
}  // namespace

void MainWindow::quickOpen() {
    if (root_.isEmpty()) {
        CodeView* v = currentView();
        const QString p = v ? QString::fromStdString(v->editor().doc().path()) : QString();
        setWorkspaceRoot(p.isEmpty() ? QDir::currentPath() : QFileInfo(p).absolutePath());
    }
    const DynArray<std::string> files = listWorkspaceFiles(root_.toUtf8().toStdString());

    QDialog dlg(this, Qt::Dialog | Qt::FramelessWindowHint);
    dlg.setStyleSheet("QDialog{border:1px solid #454545;}");
    dlg.resize(620, 360);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(6, 6, 6, 6);
    QLineEdit* input = new QLineEdit();
    input->setPlaceholderText(QString("Go to file in %1 … (e.g. \"mw\" finds MainWindow.cpp)").arg(QFileInfo(root_).fileName()));
    QListWidget* list = new QListWidget();
    lay->addWidget(input);
    lay->addWidget(list, 1);
    input->installEventFilter(new ListKeys(list, &dlg));

    auto refresh = [&]() {
        list->clear();
        const DynArray<size_t> hits = rankFiles(files, input->text().toUtf8().toStdString(), 60);
        for (size_t i = 0; i < hits.size(); ++i) list->addItem(QString::fromUtf8(files[hits[i]].c_str()));
        if (list->count() > 0) list->setCurrentRow(0);
    };
    connect(input, &QLineEdit::textChanged, &dlg, refresh);
    auto accept = [&]() {
        QListWidgetItem* it = list->currentItem();
        if (!it) return;
        dlg.setProperty("pick", it->text());
        dlg.accept();
    };
    connect(input, &QLineEdit::returnPressed, &dlg, accept);
    connect(list, &QListWidget::itemActivated, &dlg, accept);
    refresh();
    input->setFocus();

    // place near the top centre of the main window, like a command palette
    dlg.move(mapToGlobal(QPoint((width() - dlg.width()) / 2, 60)));
    if (dlg.exec() == QDialog::Accepted) {
        const QString rel = dlg.property("pick").toString();
        if (!rel.isEmpty()) openPath(QDir(root_).filePath(rel));
    }
}

// ---------------------------------------------------------------- drag and drop, session

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    for (const QUrl& u : e->mimeData()->urls()) {
        if (!u.isLocalFile()) continue;
        const QString p = u.toLocalFile();
        if (QFileInfo(p).isDir()) { setWorkspaceRoot(p); dock_->show(); }
        else openPath(p);
    }
}

void MainWindow::closeEvent(QCloseEvent* e) {
    for (int i = 0; i < tabs_->count(); ++i) {
        if (!maybeSave(viewAt(i))) { e->ignore(); return; }
    }
    QSettings s("DevPad", "DevPad");
    QStringList files;
    for (int i = 0; i < tabs_->count(); ++i) {
        const QString p = QString::fromStdString(viewAt(i)->editor().doc().path());
        if (!p.isEmpty()) files << p;
    }
    s.setValue("files", files);
    s.setValue("current", currentView() ? QString::fromStdString(currentView()->editor().doc().path()) : QString());
    s.setValue("root", root_);
    s.setValue("sidebar", dock_->isVisible());
    s.setValue("geometry", saveGeometry());
    s.setValue("zoom", currentView() ? currentView()->zoom() : 11);
    e->accept();
}

void MainWindow::restoreSession() {
    QSettings s("DevPad", "DevPad");
    restoreGeometry(s.value("geometry").toByteArray());
    const QString root = s.value("root").toString();
    if (!root.isEmpty() && QFileInfo(root).isDir()) setWorkspaceRoot(root);
    dock_->setVisible(s.value("sidebar", false).toBool() && !root_.isEmpty());
    const int zoom = s.value("zoom", 11).toInt();
    for (const QString& f : s.value("files").toStringList())
        if (QFileInfo(f).isFile()) openPath(f);
    const QString cur = s.value("current").toString();
    if (!cur.isEmpty()) { const int i = indexOfPath(cur); if (i >= 0) tabs_->setCurrentIndex(i); }
    for (int i = 0; i < tabs_->count(); ++i) viewAt(i)->setZoom(zoom);
}
