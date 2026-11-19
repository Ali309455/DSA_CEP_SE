#include "MainWindow.h"

#include <QAction>
#include <QActionGroup>
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
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
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
#include "Settings.h"
#include "SettingsDialog.h"
#include "ShortcutsDialog.h"
#include "TerminalPanel.h"
#include "ProblemsPanel.h"
#include "SnippetDialog.h"
#include "SyntaxChecker.h"
#include "Runner.h"
#include <QStandardPaths>
#include "FileIO.h"
#include "Workspace.h"

static const char* kApp = "DevPad";

// ---------------------------------------------------------------- construction

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    Theme::loadAccent();                                  // widgets built below already use the chosen accent
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
    connect(welcome_, &WelcomePage::shortcutsRequested, this, [this]() { showShortcuts(); });
    connect(welcome_, &WelcomePage::settingsRequested, this, [this]() { showSettings(); });
    connect(welcome_, &WelcomePage::terminalRequested, this, [this]() { showTerminal(true); });
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

    // Problems and Terminal under the editor (one tab strip), with a splitter to drag it taller or shorter
    terminal_ = new TerminalPanel(this);
    connect(terminal_, &TerminalPanel::hideRequested, this, [this]() { showTerminal(false); });
    problems_ = new ProblemsPanel(this);
    connect(problems_, &ProblemsPanel::hideRequested, this, [this]() { showProblems(false); });
    connect(problems_, &ProblemsPanel::jumpRequested, this, &MainWindow::jumpToProblem);
    connect(problems_, &ProblemsPanel::countsChanged, this, [this](int e, int w) {
        bottom_->setTabText(0, e + w == 0 ? QString("Problems") : QString("Problems (%1)").arg(e + w));
        if (!issueBtn_) return;
        const bool clean = e + w == 0;
        issueBtn_->setText(clean ? QString("No problems")
                                 : QString("%1 error%2, %3 warning%4").arg(e).arg(e == 1 ? "" : "s").arg(w).arg(w == 1 ? "" : "s"));
        issueBtn_->setIcon(clean ? Icons::get(Icons::Check, Theme::Success, Theme::Success)
                                 : Icons::get(Icons::Warning, Theme::Warning, Theme::Warning));
        issueBtn_->setToolTip(clean ? QString() : QString("Show the Problems panel"));
    });
    bottom_ = new QTabWidget(this);
    bottom_->setDocumentMode(true);
    bottom_->addTab(problems_, "Problems");
    bottom_->addTab(terminal_, "Terminal");
    connect(bottom_, &QTabWidget::currentChanged, this, [this]() { syncBottomActions(); });
    split_ = new QSplitter(Qt::Vertical, this);
    split_->setHandleWidth(5);
    split_->setChildrenCollapsible(false);
    split_->addWidget(centralStack_);
    split_->addWidget(bottom_);
    split_->setStretchFactor(0, 1);
    split_->setStretchFactor(1, 0);
    bottom_->hide();
    connect(split_, &QSplitter::splitterMoved, this, [this]() { if (bottom_->isVisible()) terminalHeight_ = bottom_->height(); });
    setCentralWidget(split_);

    // compiler check in the background: g++ -fsyntax-only on a copy of the current text
    checker_ = new SyntaxChecker(this);
    connect(checker_, &SyntaxChecker::finished, this, &MainWindow::onCheckFinished);
    checkTimer_ = new QTimer(this);
    checkTimer_->setSingleShot(true);
    connect(checkTimer_, &QTimer::timeout, this, &MainWindow::checkNow);
    autoSaveTimer_ = new QTimer(this);
    autoSaveTimer_->setSingleShot(true);
    connect(autoSaveTimer_, &QTimer::timeout, this, [this]() { autoSaveNow(); });

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
    connect(explorer_, &FileExplorer::closeRequested, this, [this]() { dock_->hide(); });
    dock_ = new QDockWidget("Explorer", this);
    dock_->setObjectName("explorerDock");
    dock_->setWidget(explorer_);
    dock_->setTitleBarWidget(new QWidget(dock_));        // the explorer draws its own header
    dock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock_->setMinimumWidth(170);
    addDockWidget(Qt::LeftDockWidgetArea, dock_);
    resizeDocks({dock_}, {250}, Qt::Horizontal);

    // run / stop buttons in the top right corner of the tab strip
    {
        QWidget* corner = new QWidget(tabs_);
        QHBoxLayout* cl = new QHBoxLayout(corner);
        cl->setContentsMargins(4, 0, 6, 0);
        cl->setSpacing(2);
        runBtn_ = new QToolButton(corner);
        runBtn_->setObjectName("runButton");
        runBtn_->setIcon(Icons::get(Icons::Play, Theme::Success, Theme::Success));
        runBtn_->setToolTip("Run File (F5)");
        runBtn_->setCursor(Qt::PointingHandCursor);
        stopBtn_ = new QToolButton(corner);
        stopBtn_->setObjectName("stopButton");
        stopBtn_->setIcon(Icons::get(Icons::Stop, Theme::Error, Theme::Error));
        stopBtn_->setToolTip("Stop (Shift+F5)");
        stopBtn_->setCursor(Qt::PointingHandCursor);
        stopBtn_->setEnabled(false);
        cl->addWidget(runBtn_);
        cl->addWidget(stopBtn_);
        connect(runBtn_, &QToolButton::clicked, this, [this]() { runCurrent(); });
        connect(stopBtn_, &QToolButton::clicked, this, [this]() { stopRun(); });
        tabs_->setCornerWidget(corner, Qt::TopRightCorner);
        runPoll_ = new QTimer(this);                       // Stop is available while a program (or build) runs
        runPoll_->setInterval(200);
        connect(runPoll_, &QTimer::timeout, this, [this]() {
            const bool busy = terminal_ && terminal_->busy();
            if (stopBtn_->isEnabled() != busy) stopBtn_->setEnabled(busy);
            if (runBtn_->isEnabled() == busy) runBtn_->setEnabled(!busy);
        });
        runPoll_->start();
    }

    // status bar
    posLabel_ = new QLabel("Ln 1, Col 1");
    langLabel_ = new QLabel();
    eolLabel_ = new QLabel("LF");
    issueBtn_ = new QPushButton("No problems");
    issueBtn_->setIcon(Icons::get(Icons::Check, Theme::Success, Theme::Success));
    issueBtn_->setFlat(true);
    issueBtn_->setCursor(Qt::PointingHandCursor);
    connect(issueBtn_, &QPushButton::clicked, this, [this]() {
        CodeView* v = currentView();
        Pos p;
        if (v && v->firstBracketIssuePos(p)) v->setCursor(p);
        showProblems(true);
    });
    statusBar()->addPermanentWidget(issueBtn_);
    statusBar()->addPermanentWidget(posLabel_);
    statusBar()->addPermanentWidget(langLabel_);
    encLabel_ = new QLabel("UTF-8");
    statusBar()->addPermanentWidget(encLabel_);
    statusBar()->addPermanentWidget(eolLabel_);
    themeBtn_ = new QPushButton();
    themeBtn_->setFlat(true);
    themeBtn_->setCursor(Qt::PointingHandCursor);
    themeBtn_->setFocusPolicy(Qt::NoFocus);
    connect(themeBtn_, &QPushButton::clicked, this, [this]() { toggleTheme(); });
    statusBar()->addPermanentWidget(themeBtn_);

    buildActions();
    buildActivityBar();
    applyDarkTheme();
    enableDarkTitleBar(this);
    onThemeChanged();                                     // theme button text, checked entry in View > Theme
    connect(&Theme::notifier(), &Theme::Notifier::changed, this, &MainWindow::onThemeChanged);
    updateCentral();
    connect(&AppSettings::instance(), &AppSettings::changed, this, &MainWindow::applyPrefs);
    statusBar()->setVisible(AppSettings::instance().values().statusBar);
}

// ---------------------------------------------------------------- settings, terminal, shortcuts windows

void MainWindow::applyPrefs() {
    const AppSettings::Values& v = AppSettings::instance().values();
    statusBar()->setVisible(v.statusBar);
    if (!v.autoSave) autoSaveTimer_->stop();
    else if (autoSaveTimer_->isActive()) autoSaveTimer_->start(v.autoSaveDelay * 1000);      // a new delay applies at once
    // the theme and the accent colour are applied by Theme itself; onThemeChanged() follows
}

// Auto save: every modified file that already has a name is written (no dialogs; a failure is only reported in the status bar).
int MainWindow::autoSaveNow() {
    int saved = 0;
    QString failed;
    for (int i = 0; i < tabs_->count(); ++i) {
        CodeView* v = viewAt(i);
        const QString path = QString::fromStdString(v->editor().doc().path());
        if (path.isEmpty() || !v->editor().doc().modified()) continue;
        QString err;
        if (v->saveFile(path, &err)) { ++saved; refreshTab(v); }
        else failed = QFileInfo(path).fileName() + ": " + err;
    }
    if (saved > 0) refreshTitle();
    if (!failed.isEmpty()) statusBar()->showMessage("Auto save failed - " + failed, 6000);
    else if (saved > 0) statusBar()->showMessage(QString("Auto-saved %1 file(s)").arg(saved), 2000);
    return saved;
}

// ---------------------------------------------------------------- themes

void MainWindow::setTheme(const QString& id) {
    AppSettings::Values v = AppSettings::instance().values();
    v.theme = id;
    AppSettings::instance().apply(v);
}

void MainWindow::toggleTheme() { setTheme(Theme::toggledScheme(AppSettings::instance().values().theme)); }

void MainWindow::onThemeChanged() {
    if (welcome_) welcome_->refreshAccent();
    enableDarkTitleBar(this);
    refreshTitleBars();
    const QString id = Theme::currentScheme();
    for (QAction* a : themeActs_) a->setChecked(a->data().toString() == id);
    if (themeBtn_) {
        themeBtn_->setText(Theme::schemeName(id));
        themeBtn_->setToolTip(QString("Theme: %1. Click to switch to %2 (Ctrl+Shift+L).")
                                  .arg(Theme::schemeName(id), Theme::schemeName(Theme::toggledScheme(id))));
    }
    if (runBtn_) runBtn_->setIcon(Icons::get(Icons::Play, Theme::Success, Theme::Success));
    if (stopBtn_) stopBtn_->setIcon(Icons::get(Icons::Stop, Theme::Error, Theme::Error));
    if (issueBtn_) {
        const bool clean = issueBtn_->text() == QLatin1String("No problems");
        issueBtn_->setIcon(clean ? Icons::get(Icons::Check, Theme::Success, Theme::Success)
                                 : Icons::get(Icons::Warning, Theme::Warning, Theme::Warning));
    }
    update();
}

SettingsDialog* MainWindow::showSettings(const QString& section) {
    if (!settings_) settings_ = new SettingsDialog(this);
    settings_->show();
    settings_->raise();
    settings_->activateWindow();
    if (!section.isEmpty()) settings_->showSection(section);
    return settings_;
}

ShortcutsDialog* MainWindow::showShortcuts() {
    if (!shortcuts_) shortcuts_ = new ShortcutsDialog(this);
    shortcuts_->show();
    shortcuts_->raise();
    shortcuts_->activateWindow();
    return shortcuts_;
}

// F5. The file is saved first (the program must be what is on screen), then built and run in the
// terminal, in the file's own folder so relative paths in the program work.
bool MainWindow::runCurrent() {
    CodeView* v = currentView();
    if (!v) { statusBar()->showMessage("Open a file to run it", 4000); return false; }
    if (!saveView(v, false)) return false;
    const QString path = QString::fromStdString(v->editor().doc().path());
    const QString outDir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("devpad-run");
    QDir().mkpath(outDir);
    const Runner::Shell sh = Runner::shellKind(AppSettings::instance().values().terminalShell);
    const Runner::Plan plan = Runner::planFor(path, sh, outDir);
    showTerminal(true);
    if (plan.steps.isEmpty()) {
        const QString msg = "DevPad cannot run ." + plan.extension + " files (F5 runs: " + Runner::supportedExtensions().join(", ") + ")";
        statusBar()->showMessage(msg, 6000);
        terminal_->runSequence({});                       // nothing; the message above explains
        return false;
    }
    terminal_->setWorkingDirectory(QFileInfo(path).absolutePath());
    statusBar()->showMessage("Running " + QFileInfo(path).fileName() + " - " + plan.description, 4000);
    terminal_->runSequence(plan.steps);
    return true;
}

void MainWindow::stopRun() { if (terminal_) terminal_->stop(); }

bool MainWindow::sidebarVisible() const { return dock_ && !dock_->isHidden(); }
void MainWindow::toggleSidebar() { dock_->setVisible(!sidebarVisible()); }

bool MainWindow::bottomVisible() const { return bottom_ && !bottom_->isHidden(); }
bool MainWindow::terminalVisible() const { return bottomVisible() && bottom_->currentWidget() == terminal_; }
bool MainWindow::problemsVisible() const { return bottomVisible() && bottom_->currentWidget() == problems_; }

// Show the bottom panel with 'page' (Problems or Terminal) in front.
void MainWindow::showBottom(QWidget* page) {
    const bool wasHidden = !bottomVisible();
    bottom_->setCurrentWidget(page);
    if (wasHidden) {
        bottom_->show();
        const int total = split_->height();
        const int h = std::max(120, std::min(terminalHeight_, total / 2));
        split_->setSizes({std::max(1, total - h), h});
    }
    syncBottomActions();
}

void MainWindow::hideBottom() {
    if (!bottomVisible()) return;
    terminalHeight_ = std::max(120, bottom_->height());
    bottom_->hide();
    if (CodeView* v = currentView()) v->setFocus();
    syncBottomActions();
}

void MainWindow::syncBottomActions() {
    if (terminalAct_) {
        QSignalBlocker b(terminalAct_);
        terminalAct_->setChecked(terminalVisible());
    }
    if (problemsAct_) {
        QSignalBlocker b(problemsAct_);
        problemsAct_->setChecked(problemsVisible());
    }
}

void MainWindow::showTerminal(bool show) {
    if (show) {
        if (!terminalVisible()) showBottom(terminal_);
        terminal_->focusInput();
    } else if (terminalVisible()) {
        hideBottom();
    }
}

void MainWindow::showProblems(bool show) {
    if (show) {
        if (!problemsVisible()) showBottom(problems_);
    } else if (problemsVisible()) {
        hideBottom();
    }
}

// ---------------------------------------------------------------- problems: compiler check + list

// Check the current file with the compiler a moment after the last edit / tab change.
void MainWindow::scheduleCheck(int delayMs) {
    if (!liveCheck_) return;
    CodeView* v = currentView();
    if (!v) return;
    const std::string path = v->editor().doc().path();
    if (path.empty() || !isCompilableSource(path)) return;      // brackets are still checked live
    checkTimer_->start(delayMs);
}

void MainWindow::checkNow() {
    CodeView* v = currentView();
    if (!v) return;
    const std::string path = v->editor().doc().path();
    if (path.empty() || !isCompilableSource(path)) {
        problems_->setStatus(path.empty() ? "Save the file as .cpp or .c to check it with the compiler (brackets are checked already)."
                                          : "The compiler check is for C / C++ files (brackets are checked already).");
        return;
    }
    checkView_ = v;
    checkRevision_ = v->revision();
    problems_->setStatus("Checking…");
    checker_->check(QString::fromStdString(path), v->editor().doc().toString(), ++checkToken_);
}

void MainWindow::onCheckFinished(const QString&, quint64 token, const std::vector<Diagnostic>& diags, const QString& message) {
    if (token != checkToken_) return;
    CodeView* v = checkView_;
    if (!v) return;
    if (!message.isEmpty()) { problems_->setStatus(message); return; }
    if (v->revision() != checkRevision_) {                      // edited meanwhile: the result is out of date
        if (liveCheck_) scheduleCheck(200); else problems_->setStatus(QString());
        return;
    }
    v->setCompilerDiagnostics(diags);
    problems_->setStatus(QString());
}

void MainWindow::refreshProblems() {
    CodeView* v = currentView();
    if (!v) {
        problems_->setProblems({}, QString());
        problems_->setStatus(QString());
        return;
    }
    QString name = QString::fromStdString(v->editor().doc().path());
    if (name.isEmpty()) name = v->property("untitledName").toString();
    problems_->setProblems(v->diagnostics(), name);
}

// A row of the Problems list was clicked: open the file (if it is another one) and go there.
void MainWindow::jumpToProblem(const QString& file, int line, int col) {
    CodeView* v = currentView();
    if (!v) return;
    const QString own = QString::fromStdString(v->editor().doc().path());
    bool same = file.isEmpty() || file == own || file == v->property("untitledName").toString();
    if (!same && !own.isEmpty()) {
        QFileInfo target(file);
        if (target.isRelative()) target = QFileInfo(QDir(QFileInfo(own).absolutePath()).filePath(file));
        if (target.isFile() && target == QFileInfo(own)) same = true;
        else if (target.isFile() && openPath(target.absoluteFilePath())) { v = currentView(); same = (v != nullptr); }
    }
    if (!same || !v) return;
    if (line > 0) v->setCursor(Pos{static_cast<size_t>(line - 1), static_cast<size_t>(col > 0 ? col - 1 : 0)});
    v->setFocus();
}

void MainWindow::editSnippets() {
    SnippetDialog dlg(this);
    dlg.exec();
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

    QAction* side = dock_->toggleViewAction();             // checkable: shows whether the Explorer is open
    side->setIcon(Icons::get(Icons::Files));
    side->setToolTip("Explorer (Ctrl+B)");
    bar->addAction(side);
    QAction* find = bar->addAction(Icons::get(Icons::Search), "Find");
    find->setToolTip("Find in File (Ctrl+F)");
    connect(find, &QAction::triggered, this, [this]() { showFind(false); });
    QAction* go = bar->addAction(Icons::get(Icons::Palette), "Go to File");
    go->setToolTip("Go to File (Ctrl+P)");
    connect(go, &QAction::triggered, this, &MainWindow::quickOpen);
    QAction* playAct = bar->addAction(Icons::get(Icons::Play), "Run File");
    playAct->setToolTip("Run File (F5)");
    connect(playAct, &QAction::triggered, this, [this]() { runCurrent(); });
    terminalAct_ = bar->addAction(Icons::get(Icons::Terminal), "Terminal");
    terminalAct_->setCheckable(true);
    terminalAct_->setToolTip("Terminal (Ctrl+`)");
    connect(terminalAct_, &QAction::triggered, this, [this](bool on) { showTerminal(on); });
    QWidget* spacer = new QWidget(bar);
    spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    bar->addWidget(spacer);
    QAction* keys = bar->addAction(Icons::get(Icons::Keyboard), "Keyboard Shortcuts");
    keys->setToolTip("Keyboard Shortcuts (F1)");
    connect(keys, &QAction::triggered, this, [this]() { showShortcuts(); });
    QAction* gear = bar->addAction(Icons::get(Icons::Gear), "Settings");
    gear->setToolTip("Settings (Ctrl+,)");
    connect(gear, &QAction::triggered, this, [this]() { showSettings(); });
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
        if (problems_) refreshProblems();
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
    add(file, "Settings…", seq("Ctrl+,"), [this]() { showSettings(); });
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
    add(edit, "Expand Snippet", seq("Ctrl+J"), onView(&CodeView::expandSnippet));
    add(edit, "Manage Snippets…", {}, [this]() { editSnippets(); });

    QMenu* search = menuBar()->addMenu("&Search");
    add(search, "Find…", {QKeySequence::Find}, [this]() { showFind(false); });
    add(search, "Replace…", seq("Ctrl+H"), [this]() { showFind(true); });
    add(search, "Find Next", {QKeySequence(QStringLiteral("F3"))}, [this]() { findNext(false); });
    add(search, "Find Previous", {QKeySequence(QStringLiteral("Shift+F3"))}, [this]() { findNext(true); });
    search->addSeparator();
    add(search, "Go to File…", {QKeySequence(QStringLiteral("Ctrl+P"))}, [this]() { quickOpen(); });
    add(search, "Go to Line…", seq("Ctrl+G"), [this]() { goToLineDialog(); });

    QMenu* run = menuBar()->addMenu("&Run");
    runAct_ = add(run, "Run File", {QKeySequence(QStringLiteral("F5"))}, [this]() { runCurrent(); });
    add(run, "Stop", {QKeySequence(QStringLiteral("Shift+F5"))}, [this]() { stopRun(); });
    run->addSeparator();
    add(run, "Check for Errors", seq("F7"), [this]() { showProblems(true); checkNow(); });
    liveCheck_ = QSettings().value("liveCheck", true).toBool();
    QAction* liveAct = run->addAction("Check Code While Typing");
    liveAct->setCheckable(true);
    liveAct->setChecked(liveCheck_);
    addAction(liveAct);
    connect(liveAct, &QAction::toggled, this, [this](bool on) {
        liveCheck_ = on;
        QSettings().setValue("liveCheck", on);
        if (on) { scheduleCheck(200); return; }
        checkTimer_->stop();
        checker_->cancel();
        if (CodeView* v = currentView()) v->clearCompilerDiagnostics();
        problems_->setStatus(QString());
    });

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
    add(view, "Toggle Terminal", seq("Ctrl+`"), [this]() { showTerminal(!terminalVisible()); });
    problemsAct_ = add(view, "Toggle Problems", seq("Ctrl+Shift+M"), [this]() { showProblems(!problemsVisible()); });
    problemsAct_->setCheckable(true);
    add(view, "Toggle Sidebar", seq("Ctrl+B"), [this]() { toggleSidebar(); });
    auto zoomBy = [this](int d) {                          // zoom is the font-size setting, shared by every tab
        const int target = d == 0 ? AppSettings::Values().fontSize : AppSettings::instance().values().fontSize + d;
        AppSettings::Values v = AppSettings::instance().values();
        v.fontSize = target;
        AppSettings::instance().apply(v);
    };
    add(view, "Zoom In", {QKeySequence(QStringLiteral("Ctrl+=")), QKeySequence(QStringLiteral("Ctrl++"))}, [zoomBy]() { zoomBy(+1); });
    add(view, "Zoom Out", seq("Ctrl+-"), [zoomBy]() { zoomBy(-1); });
    add(view, "Reset Zoom", seq("Ctrl+0"), [zoomBy]() { zoomBy(0); });
    view->addSeparator();
    QMenu* themeMenu = view->addMenu("Theme");
    QActionGroup* themeGroup = new QActionGroup(this);
    for (int i = 0; i < Theme::schemeCount(); ++i) {
        const Theme::SchemeInfo t = Theme::schemeInfo(i);
        QAction* a = themeMenu->addAction(t.name + (t.dark ? "  (dark)" : "  (light)"));
        a->setCheckable(true);
        a->setData(t.id);
        a->setChecked(t.id == Theme::currentScheme());
        themeGroup->addAction(a);
        themeActs_.append(a);
        const QString id = t.id;
        connect(a, &QAction::triggered, this, [this, id]() { setTheme(id); });
    }
    add(view, "Toggle Light / Dark Theme", seq("Ctrl+Shift+L"), [this]() { toggleTheme(); });

    QMenu* help = menuBar()->addMenu("&Help");
    shortcutsAct_ = add(help, "Keyboard Shortcuts", seq("F1"), [this]() { showShortcuts(); });
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
    find_->addAction(Icons::get(Icons::Search), QLineEdit::LeadingPosition);
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
    findCount_->setStyleSheet(matches_.empty() ? QString("color:%1;").arg(Theme::Error.name()) : QString());
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
        if (v == currentView()) scheduleCheck();
        const AppSettings::Values& pv = AppSettings::instance().values();
        if (pv.autoSave && !v->editor().doc().path().empty()) autoSaveTimer_->start(pv.autoSaveDelay * 1000);   // restarts after every edit
    });
    connect(v, &CodeView::diagnosticsChanged, this, [this, v]() { if (v == currentView()) refreshProblems(); });
    connect(v, &CodeView::statusMessage, this, [this](const QString& m) { if (!m.isEmpty()) statusBar()->showMessage(m, 4000); });
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
    x->setIcon(Icons::get(Icons::Close));
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
    refreshProblems();
    scheduleCheck(150);
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
    if (v == currentView()) scheduleCheck(200);                 // a new name / extension may now be checkable
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
    terminal_->setWorkingDirectory(root_);
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
    dlg.setStyleSheet(QString("QDialog#quickOpen{background:%1;border:1px solid %2;}"
                              "QListWidget{background:%1;border:0;}"
                              "QListWidget::item{padding:5px 6px;border-radius:5px;}"
                              "QListWidget::item:selected{background:%3;color:%4;}")
                          .arg(Theme::Popup.name(), Theme::InputBorder.name(), Theme::accentDeep().name(), Theme::selectionText().name()));
    dlg.resize(640, 380);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(6, 6, 6, 6);
    QLineEdit* input = new QLineEdit();
    input->addAction(Icons::get(Icons::Search), QLineEdit::LeadingPosition);
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
    s.setValue("terminal", terminalVisible());
    s.setValue("problems", problemsVisible());
    s.setValue("terminalHeight", bottomVisible() ? bottom_->height() : terminalHeight_);
    e->accept();
}

void MainWindow::restoreSession() {
    QSettings s;
    restoreGeometry(s.value("geometry").toByteArray());
    terminalHeight_ = std::max(120, s.value("terminalHeight", 240).toInt());
    if (!AppSettings::instance().values().restoreSession) return;      // Settings > General: start empty
    const QString root = s.value("root").toString();
    if (!root.isEmpty() && QFileInfo(root).isDir()) setWorkspaceRoot(root);
    dock_->setVisible(s.value("sidebar", true).toBool());
    for (const QString& f : s.value("files").toStringList())
        if (QFileInfo(f).isFile()) openPath(f);
    const QString cur = s.value("current").toString();
    if (!cur.isEmpty()) { const int i = indexOfPath(cur); if (i >= 0) tabs_->setCurrentIndex(i); }
    if (s.value("terminal", false).toBool()) showTerminal(true);
    else if (s.value("problems", false).toBool()) showProblems(true);
}
