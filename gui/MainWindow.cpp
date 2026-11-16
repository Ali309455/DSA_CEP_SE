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
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

#include "CodeView.h"
#include "FileExplorer.h"
#include "WelcomePage.h"
#include "Icons.h"
#include "Theme.h"
#include "WinTheme.h"
#include "FileIO.h"
#include "Workspace.h"

static const char* kApp = "DevPad";

// ---------------------------------------------------------------- construction

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(kApp);
    setWindowIcon(Icons::app());
    setAcceptDrops(true);
    resize(1200, 780);

    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    tabs_->setTabsClosable(false);                        // own close buttons (see newTab), drawn to match the theme
    tabs_->setIconSize(QSize(16, 16));
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
    editorArea_ = new QWidget(this);
    QVBoxLayout* lay = new QVBoxLayout(editorArea_);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(tabs_, 1);
    lay->addWidget(findBar_);

    // no file open: the welcome page takes the editor's place
    welcome_ = new WelcomePage(this);
    connect(welcome_, &WelcomePage::newFileRequested, this, [this]() { newTab(); });
    connect(welcome_, &WelcomePage::openFileRequested, this, &MainWindow::openDialog);
    connect(welcome_, &WelcomePage::openFolderRequested, this, &MainWindow::openFolderDialog);
    connect(welcome_, &WelcomePage::quickOpenRequested, this, &MainWindow::quickOpen);
    connect(welcome_, &WelcomePage::shortcutsRequested, this, [this]() { if (shortcutsAct_) shortcutsAct_->trigger(); });
    connect(welcome_, &WelcomePage::recentFileRequested, this, [this](const QString& p) { openPath(p); });
    connect(welcome_, &WelcomePage::recentFolderRequested, this, [this](const QString& p) {
        if (!QFileInfo(p).isDir()) { updateCentral(); return; }
        setWorkspaceRoot(p);
        addRecentFolder(p);
        dock_->show();
    });
    centralStack_ = new QStackedWidget(this);
    centralStack_->addWidget(welcome_);
    centralStack_->addWidget(editorArea_);
    setCentralWidget(centralStack_);

    // sidebar: file manager
    explorer_ = new FileExplorer(this);
    connect(explorer_, &FileExplorer::openRequested, this, [this](const QString& p) { openPath(p); });
    connect(explorer_, &FileExplorer::pathRenamed, this, &MainWindow::onPathRenamed);
    connect(explorer_, &FileExplorer::pathRemoved, this, &MainWindow::onPathRemoved);
    connect(explorer_, &FileExplorer::openFolderRequested, this, &MainWindow::openFolderDialog);
    explorer_->unsavedCount = [this](const QString& path) {
        int n = 0;
        for (int i = 0; i < tabs_->count(); ++i) {
            const std::string p = viewAt(i)->editor().doc().path();
            if (!p.empty() && viewAt(i)->editor().doc().modified() && isSameOrInside(p, path.toUtf8().toStdString())) ++n;
        }
        return n;
    };
    dock_ = new QDockWidget("Explorer", this);
    dock_->setObjectName("explorerDock");
    dock_->setWidget(explorer_);
    dock_->setTitleBarWidget(new QWidget(dock_));        // the explorer draws its own header
    dock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock_->setMinimumWidth(170);
    addDockWidget(Qt::LeftDockWidgetArea, dock_);
    resizeDocks({dock_}, {250}, Qt::Horizontal);

    // status bar
    posLabel_ = new QLabel("Ln 1, Col 1");
    langLabel_ = new QLabel();
    eolLabel_ = new QLabel("LF");
    issueBtn_ = new QPushButton("Brackets OK");
    issueBtn_->setIcon(Icons::get(Icons::Check, QColor(0x73, 0xc9, 0x91), QColor(0x73, 0xc9, 0x91)));
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
    encLabel_ = new QLabel("UTF-8");
    statusBar()->addPermanentWidget(encLabel_);
    statusBar()->addPermanentWidget(eolLabel_);

    buildActions();
    buildActivityBar();
    applyDarkTheme();
    enableDarkTitleBar(this);
    updateCentral();
}

// Slim bar on the far left with the main places, like other modern editors.
void MainWindow::buildActivityBar() {
    QToolBar* bar = new QToolBar("Activity Bar", this);
    bar->setObjectName("activityBar");
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->setOrientation(Qt::Vertical);
    bar->setIconSize(QSize(24, 24));
    bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    bar->setContextMenuPolicy(Qt::PreventContextMenu);
    bar->toggleViewAction()->setVisible(false);
    addToolBar(Qt::LeftToolBarArea, bar);

    const QColor rest(0x85, 0x85, 0x85), lit(0xf0, 0xf0, 0xf0);
    QAction* side = dock_->toggleViewAction();             // checkable: shows whether the Explorer is open
    side->setIcon(Icons::get(Icons::Files, rest, lit));
    side->setToolTip("Explorer (Ctrl+B)");
    bar->addAction(side);
    QAction* find = bar->addAction(Icons::get(Icons::Search, rest, lit), "Find");
    find->setToolTip("Find in File (Ctrl+F)");
    connect(find, &QAction::triggered, this, [this]() { showFind(false); });
    QAction* go = bar->addAction(Icons::get(Icons::Palette, rest, lit), "Go to File");
    go->setToolTip("Go to File (Ctrl+P)");
    connect(go, &QAction::triggered, this, &MainWindow::quickOpen);
    QWidget* spacer = new QWidget(bar);
    spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    bar->addWidget(spacer);
    QAction* keys = bar->addAction(Icons::get(Icons::Keyboard, rest, lit), "Keyboard Shortcuts");
    keys->setToolTip("Keyboard Shortcuts (F1)");
    connect(keys, &QAction::triggered, this, [this]() { if (shortcutsAct_) shortcutsAct_->trigger(); });
}

// ---------------------------------------------------------------- welcome page, recent lists

bool MainWindow::welcomeShown() const { return centralStack_->currentWidget() == welcome_; }

void MainWindow::updateCentral() {
    const bool any = tabs_->count() > 0;
    centralStack_->setCurrentWidget(any ? editorArea_ : welcome_);
    for (QWidget* w : std::initializer_list<QWidget*>{posLabel_, langLabel_, eolLabel_, encLabel_, issueBtn_}) w->setVisible(any);
    if (!any) {
        findBar_->hide();
        matches_.clear();
        welcome_->setRecent(recentFiles(), recentFolders());
        welcome_->setFolderOpen(!root_.isEmpty());
        refreshTitle();
    }
}

static QStringList existingOnly(const QStringList& paths) {
    QStringList out;
    for (const QString& p : paths) if (QFileInfo::exists(p)) out << p;
    return out;
}

QStringList MainWindow::recentFiles() const { return existingOnly(QSettings().value("recentFiles").toStringList()); }
QStringList MainWindow::recentFolders() const { return existingOnly(QSettings().value("recentFolders").toStringList()); }

void MainWindow::addRecent(const char* key, const QString& path) {
    if (path.isEmpty()) return;
    const QString p = QFileInfo(path).absoluteFilePath();
    QSettings s;
    QStringList list = s.value(key).toStringList();
    for (int i = list.size() - 1; i >= 0; --i)
        if (QFileInfo(list[i]) == QFileInfo(p) || list[i].compare(p, Qt::CaseInsensitive) == 0) list.removeAt(i);
    list.prepend(p);
    while (list.size() > 12) list.removeLast();
    s.setValue(key, list);
    if (welcome_ && welcomeShown()) welcome_->setRecent(recentFiles(), recentFolders());
}

void MainWindow::addRecentFolder(const QString& dir) { addRecent("recentFolders", dir); }

void MainWindow::applyDarkTheme() {
    Theme::apply();
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
    shortcutsAct_ = add(help, "Keyboard Shortcuts", seq("F1"), [this]() {
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
            "Explorer (sidebar, when it has focus)\n"
            "  click / Enter               open file, open or close folder\n"
            "  F2                          rename        Del  delete (Recycle Bin)\n"
            "  Shift+Del                   delete permanently\n"
            "  Ctrl+X / Ctrl+C / Ctrl+V    cut / copy / paste files\n"
            "  drag onto a folder          move  (hold Ctrl to copy)\n"
            "  right-click                 new file / folder, duplicate, copy path, reveal\n\n"
            "Lines\n"
            "  Alt+Up / Alt+Down           move line(s)\n"
            "  Shift+Alt+Up / Down         copy line(s) up / down  (Ctrl+D also copies down)\n"
            "  Ctrl+Shift+K                delete line\n"
            "  Ctrl+Enter / Ctrl+Shift+Enter   new line below / above\n"
            "  Ctrl+] / Ctrl+[             indent / outdent\n"
            "  Ctrl+/                      toggle comment\n"
            "  Ctrl+L                      select line\n\n"
            "Brackets and quotes\n"
            "  type ( [ { \" ' ` with text selected   wrap the selection (the same key again unwraps)\n"
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
    findBar_->setObjectName("findBar");
    QVBoxLayout* v = new QVBoxLayout(findBar_);
    v->setContentsMargins(10, 7, 10, 7);
    v->setSpacing(6);

    auto iconButton = [this](Icons::Glyph g, const QString& tip, bool checkable) {
        QToolButton* b = new QToolButton(findBar_);
        b->setIcon(Icons::get(g));
        b->setIconSize(QSize(16, 16));
        b->setFixedSize(28, 28);
        b->setToolTip(tip);
        b->setCheckable(checkable);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };

    QHBoxLayout* r1 = new QHBoxLayout();
    r1->setSpacing(4);
    find_ = new QLineEdit();
    find_->setPlaceholderText("Find");
    find_->addAction(Icons::get(Icons::Search, QColor(0x80, 0x80, 0x80)), QLineEdit::LeadingPosition);
    caseBox_ = iconButton(Icons::CaseSensitive, "Match case", true);
    wordBox_ = iconButton(Icons::WholeWord, "Match whole word", true);
    findCount_ = new QLabel();
    findCount_->setObjectName("findCount");
    findCount_->setMinimumWidth(90);
    findCount_->setAlignment(Qt::AlignCenter);
    QToolButton* prev = iconButton(Icons::ArrowUp, "Previous match (Shift+F3)", false);
    QToolButton* next = iconButton(Icons::ArrowDown, "Next match (F3)", false);
    QToolButton* close = iconButton(Icons::Close, "Close (Esc)", false);
    r1->addWidget(find_, 1);
    r1->addWidget(caseBox_);
    r1->addWidget(wordBox_);
    r1->addSpacing(4);
    r1->addWidget(findCount_);
    r1->addWidget(prev);
    r1->addWidget(next);
    r1->addWidget(close);
    v->addLayout(r1);

    replRow_ = new QWidget();
    QHBoxLayout* r2 = new QHBoxLayout(replRow_);
    r2->setContentsMargins(0, 0, 0, 0);
    r2->setSpacing(6);
    repl_ = new QLineEdit();
    repl_->setPlaceholderText("Replace");
    QPushButton* one = new QPushButton("Replace");
    QPushButton* all = new QPushButton("Replace All");
    r2->addWidget(repl_, 1);
    r2->addWidget(one);
    r2->addWidget(all);
    r2->addSpacing(28 * 3 + 8);                         // line the field up with the one above
    v->addWidget(replRow_);

    connect(find_, &QLineEdit::textChanged, this, &MainWindow::updateFind);
    connect(find_, &QLineEdit::returnPressed, this, [this]() { findNext(false); });
    connect(repl_, &QLineEdit::returnPressed, this, &MainWindow::replaceOne);
    connect(caseBox_, &QToolButton::toggled, this, &MainWindow::updateFind);
    connect(wordBox_, &QToolButton::toggled, this, &MainWindow::updateFind);
    connect(prev, &QToolButton::clicked, this, [this]() { findNext(true); });
    connect(next, &QToolButton::clicked, this, [this]() { findNext(false); });
    connect(close, &QToolButton::clicked, this, &MainWindow::closeFind);
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
    QToolButton* x = new QToolButton(tabs_->tabBar());
    x->setObjectName("tabClose");
    x->setIcon(Icons::get(Icons::Close, QColor(0x9d, 0x9d, 0x9d), QColor(0xff, 0xff, 0xff)));
    x->setIconSize(QSize(14, 14));
    x->setFixedSize(20, 20);
    x->setAutoRaise(true);
    x->setToolTip("Close (Ctrl+W)");
    x->setFocusPolicy(Qt::NoFocus);
    connect(x, &QToolButton::clicked, this, [this, v]() { closeTab(tabs_->indexOf(v)); });
    tabs_->tabBar()->setTabButton(i, QTabBar::RightSide, x);
    tabs_->setCurrentIndex(i);
    refreshTab(v);
    updateCentral();
    v->setFocus();
    if (!path.isEmpty()) addRecent("recentFiles", path);
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
    if (existing >= 0) { tabs_->setCurrentIndex(existing); addRecent("recentFiles", fi.absoluteFilePath()); return true; }
    if (root_.isEmpty()) setWorkspaceRoot(fi.absolutePath());
    // a single untouched "Untitled" tab is replaced instead of left behind
    CodeView* cur = currentView();
    if (tabs_->count() == 1 && cur && cur->editor().doc().path().empty() &&
        !cur->editor().doc().modified() && cur->editor().doc().isEmpty()) {
        QString err;
        if (!cur->openFile(fi.absoluteFilePath(), &err)) { QMessageBox::warning(this, kApp, err); return false; }
        refreshTab(cur);
        onTabChanged(0);
        addRecent("recentFiles", fi.absoluteFilePath());
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
    tabs_->setTabIcon(i, Icons::forFile(path.isEmpty() ? QStringLiteral("untitled.txt") : path));
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
    explorer_->reveal(QString::fromStdString(v->editor().doc().path()));
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
        issueBtn_->setText(n == 0 ? QString("Brackets OK") : QString("%1 bracket issue%2").arg(n).arg(n == 1 ? "" : "s"));
        issueBtn_->setIcon(n == 0 ? Icons::get(Icons::Check, QColor(0x73, 0xc9, 0x91), QColor(0x73, 0xc9, 0x91))
                                  : Icons::get(Icons::Warning, QColor(0xe2, 0xb7, 0x14), QColor(0xe2, 0xb7, 0x14)));
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
    updateCentral();                                    // the last tab closed: back to the welcome page
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
    addRecent("recentFiles", path);
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
    if (!d.isEmpty()) { setWorkspaceRoot(d); addRecentFolder(d); dock_->show(); }
}

void MainWindow::setWorkspaceRoot(const QString& dir) {
    const QFileInfo fi(dir);
    if (!fi.isDir()) return;
    root_ = fi.canonicalFilePath();
    rootChosen_ = true;
    explorer_->setRoot(root_);
    welcome_->setFolderOpen(true);
    if (CodeView* v = currentView()) explorer_->reveal(QString::fromStdString(v->editor().doc().path()));
}

// The explorer renamed or moved a file or folder: tabs showing it (or anything inside it) follow.
// Unsaved edits and undo history stay; only the path changes.
static QString cleanPath(const QString& p) { return QDir::cleanPath(QDir::fromNativeSeparators(p)); }

static bool pathAtOrBelow(const QString& path, const QString& base) {
    const Qt::CaseSensitivity cs =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    const QString p = cleanPath(path), b = cleanPath(base);
    return p.compare(b, cs) == 0 || (p.startsWith(b, cs) && p.size() > b.size() && p[b.size()] == '/');
}

void MainWindow::onPathRenamed(const QString& oldPath, const QString& newPath) {
    for (int i = 0; i < tabs_->count(); ++i) {
        CodeView* v = viewAt(i);
        const QString p = QString::fromStdString(v->editor().doc().path());
        if (p.isEmpty() || !pathAtOrBelow(p, oldPath)) continue;
        const QString np = cleanPath(newPath) + cleanPath(p).mid(cleanPath(oldPath).size());
        v->editor().setPath(np.toStdString());
        v->setLanguageForPath(np);
        refreshTab(v);
    }
    refreshTitle();
    onViewStateChanged();
}

// The explorer deleted a file or folder (the user confirmed, unsaved tabs included): close its tabs.
void MainWindow::onPathRemoved(const QString& path) {
    for (int i = tabs_->count() - 1; i >= 0; --i) {
        const QString p = QString::fromStdString(viewAt(i)->editor().doc().path());
        if (!p.isEmpty() && pathAtOrBelow(p, path)) closeTab(i, true);
    }
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
    dlg.setObjectName("quickOpen");
    dlg.setStyleSheet("QDialog#quickOpen{background:#1f1f1f;border:1px solid #3a3a3a;}"
                      "QListWidget{background:#1f1f1f;border:0;}"
                      "QListWidget::item{padding:5px 6px;border-radius:5px;}"
                      "QListWidget::item:selected{background:#233a5e;color:#ffffff;}");
    dlg.resize(640, 380);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(6, 6, 6, 6);
    QLineEdit* input = new QLineEdit();
    input->addAction(Icons::get(Icons::Search, QColor(0x80, 0x80, 0x80)), QLineEdit::LeadingPosition);
    input->setPlaceholderText(QString("Go to file in %1 … (e.g. \"mw\" finds MainWindow.cpp)").arg(QFileInfo(root_).fileName()));
    QListWidget* list = new QListWidget();
    lay->addWidget(input);
    lay->addWidget(list, 1);
    input->installEventFilter(new ListKeys(list, &dlg));

    auto refresh = [&]() {
        list->clear();
        const DynArray<size_t> hits = rankFiles(files, input->text().toUtf8().toStdString(), 60);
        for (size_t i = 0; i < hits.size(); ++i) {
            const QString rel = QString::fromUtf8(files[hits[i]].c_str());
            new QListWidgetItem(Icons::forFile(rel), rel, list);
        }
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
        if (QFileInfo(p).isDir()) { setWorkspaceRoot(p); addRecentFolder(p); dock_->show(); }
        else openPath(p);
    }
}

void MainWindow::closeEvent(QCloseEvent* e) {
    for (int i = 0; i < tabs_->count(); ++i) {
        if (!maybeSave(viewAt(i))) { e->ignore(); return; }
    }
    QSettings s;
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
    QSettings s;
    restoreGeometry(s.value("geometry").toByteArray());
    const QString root = s.value("root").toString();
    if (!root.isEmpty() && QFileInfo(root).isDir()) setWorkspaceRoot(root);
    dock_->setVisible(s.value("sidebar", true).toBool());
    const int zoom = s.value("zoom", 11).toInt();
    for (const QString& f : s.value("files").toStringList())
        if (QFileInfo(f).isFile()) openPath(f);
    const QString cur = s.value("current").toString();
    if (!cur.isEmpty()) { const int i = indexOfPath(cur); if (i >= 0) tabs_->setCurrentIndex(i); }
    for (int i = 0; i < tabs_->count(); ++i) viewAt(i)->setZoom(zoom);
}
